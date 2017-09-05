/* -LICENSE-START-
 ** Copyright (c) 2012 Blackmagic Design
 **
 ** Permission is hereby granted, free of charge, to any person or organization
 ** obtaining a copy of the software and accompanying documentation covered by
 ** this license (the "Software") to use, reproduce, display, distribute,
 ** execute, and transmit the Software, and to prepare derivative works of the
 ** Software, and to permit third-parties to whom the Software is furnished to
 ** do so, all subject to the following:
 **
 ** The copyright notices in the Software and this entire statement, including
 ** the above license grant, this restriction and the following disclaimer,
 ** must be included in all copies of the Software, in whole or in part, and
 ** all derivative works of the Software, unless such copies or derivative
 ** works are solely in the form of machine-executable object code generated by
 ** a source language processor.
 **
 ** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 ** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 ** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 ** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 ** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 ** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 ** DEALINGS IN THE SOFTWARE.
 ** -LICENSE-END-
 */

#include "OpenGLCapture.h"
#include "GLExtensions.h"
#include <GL/glu.h>
#include <QtOpenGL/QGLWidget>
#include <string>
#include <sstream>

namespace patch
{
    template < typename T > std::string to_string( const T& n )
    {
        std::ostringstream stm ;
        stm << n ;
        return stm.str() ;
    }
}

OpenGLCapture::OpenGLCapture(QWidget *parent) :
	QGLWidget(parent), mParent(parent),
    mCaptureDelegate(NULL),
    mDLInput(NULL),
    mCaptureAllocator(NULL),
	mFrameWidth(0), mFrameHeight(0),
	mHasNoInputSource(true),
	mPinnedMemoryExtensionAvailable(false),
	mTexture(0),
    //VR
    m_meshWidth(20), m_meshHeight(20), m_bufferScale(0.5)
{
	ResolveGLExtensions(context());

	// Register non-builtin types for connecting signals and slots using these types
	qRegisterMetaType<IDeckLinkVideoInputFrame*>("IDeckLinkVideoInputFrame*");
	qRegisterMetaType<IDeckLinkVideoFrame*>("IDeckLinkVideoFrame*");
	qRegisterMetaType<BMDOutputFrameCompletionResult>("BMDOutputFrameCompletionResult");

    //VR
    m_deviceInfo = new DeviceInfo();
    setTextureBounds();
    computeMeshVertices(m_meshWidth, m_meshHeight);
    computeMeshIndices(m_meshWidth, m_meshHeight);
}

OpenGLCapture::~OpenGLCapture()
{
	if (mDLInput != NULL)
	{
		// Cleanup for Capture
		mDLInput->SetCallback(NULL);

		mDLInput->Release();
		mDLInput = NULL;
	}

	delete mCaptureDelegate;
	delete mCaptureAllocator;
}

int OpenGLCapture::getDeviceList(std::vector<std::string>& devices)
{
    devices.clear();

    HRESULT result = E_FAIL;
    IDeckLinkIterator* deckLinkIterator = CreateDeckLinkIteratorInstance();

    IDeckLink* deckLink = NULL;
    char* deckLinkName = NULL;

    // Loop through all available devices
    while (deckLinkIterator->Next(&deckLink) == S_OK)
    {
        result = deckLink->GetModelName((const char**)&deckLinkName);
        if (result == S_OK)
        {
            devices.push_back(deckLinkName);
            //fprintf(stderr, "        %2d: %s%s\n", deckLinkCount, deckLinkName, deckLinkCount == m_deckLinkIndex ? " (selected)" : "");
            free(deckLinkName);
        }
        deckLink->Release();
    }

    if (deckLinkIterator != NULL)
        deckLinkIterator->Release();

    return 0;
}

 IDeckLink* OpenGLCapture::getDeckLink(int idx)
 {
     HRESULT			result;
     IDeckLink*			deckLink;
     IDeckLinkIterator*	deckLinkIterator = CreateDeckLinkIteratorInstance();
     int				i = idx;

     while((result = deckLinkIterator->Next(&deckLink)) == S_OK)
     {
         if (i == 0)
             break;
         --i;

         deckLink->Release();
     }

     deckLinkIterator->Release();

     if (result != S_OK)
         return NULL;

     return deckLink;
}

 IDeckLinkDisplayMode* OpenGLCapture::getDeckLinkDisplayMode(IDeckLink* deckLink, int idx)
 {
     HRESULT						result;
     IDeckLinkDisplayMode*			displayMode = NULL;
     IDeckLinkInput*				deckLinkInput = NULL;
     IDeckLinkDisplayModeIterator*	displayModeIterator = NULL;
     int							i = idx;

     result = deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput);
     if (result != S_OK)
         goto bail;

     result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
     if (result != S_OK)
         goto bail;

     while ((result = displayModeIterator->Next(&displayMode)) == S_OK)
     {
         if (i == 0)
             break;
         --i;

         displayMode->Release();
     }

     if (result != S_OK)
         goto bail;

 bail:
     if (displayModeIterator)
         displayModeIterator->Release();

     if (deckLinkInput)
         deckLinkInput->Release();

     return displayMode;
 }

int OpenGLCapture::getModeList(int device, std::vector<std::string>& modes)
{
    HRESULT result =                E_FAIL;
    IDeckLinkDisplayModeIterator*	displayModeIterator = NULL;
    IDeckLinkAttributes*			deckLinkAttributes = NULL;
    bool							formatDetectionSupported;
    IDeckLinkInput*					deckLinkInput = NULL;
    IDeckLinkDisplayMode*			displayModeUsage;
    int								displayModeCount = 0;
    char*							displayModeName;

    IDeckLink* deckLinkSelected = getDeckLink(device);

    if(!deckLinkSelected)
        return 1;

    modes.clear();

    result = deckLinkSelected->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
    if (result == S_OK)
    {
        result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &formatDetectionSupported);
        if (result == S_OK && formatDetectionSupported)
            fprintf(stderr, "        -1:  auto detect format\n");
    }

    result = deckLinkSelected->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput);
    if (result != S_OK)
        goto bail;

    result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
    if (result != S_OK)
        goto bail;

    while (displayModeIterator->Next(&displayModeUsage) == S_OK)
    {
        result = displayModeUsage->GetName((const char **)&displayModeName);
        if (result == S_OK)
        {
            BMDTimeValue frameRateDuration;
            BMDTimeValue frameRateScale;

            displayModeUsage->GetFrameRate(&frameRateDuration, &frameRateScale);
            string tmp = displayModeName;
            tmp.append(" ");
            tmp.append(patch::to_string(displayModeUsage->GetWidth()));
            tmp.append("x");
            tmp.append(patch::to_string(displayModeUsage->GetHeight()));
            double fps = (double)frameRateScale / (double)frameRateDuration;
            tmp.append(" ");
            tmp.append(patch::to_string(fps));
            tmp.append("fps");
            modes.push_back(tmp);

            free(displayModeName);
        }

        displayModeUsage->Release();
        ++displayModeCount;
    }

    return 0;

    bail:
        if (displayModeIterator != NULL)
            displayModeIterator->Release();

        if (deckLinkInput != NULL)
            deckLinkInput->Release();

        if (deckLinkAttributes != NULL)
            deckLinkAttributes->Release();

        if (deckLinkSelected != NULL)
            deckLinkSelected->Release();

        return 1;
}

bool OpenGLCapture::InitDeckLink(int device, int mode)
{
	bool							bSuccess = false;
	IDeckLinkIterator*				pDLIterator = NULL;
	IDeckLink*						pDL = NULL;
    IDeckLinkDisplayModeIterator*	pDLDisplayModeIterator = NULL;
    IDeckLinkDisplayMode*			pDLDisplayMode = NULL;
    BMDDisplayMode					displayMode = bmdModeHD1080i6000; //bmdModeHD1080i5994; //bmdModeHD1080p30; //bmdModeHD1080i6000;
	float							fps;

	pDLIterator = CreateDeckLinkIteratorInstance();
	if (pDLIterator == NULL)
	{
		QMessageBox::critical(NULL, "This application requires the DeckLink drivers installed.", "Please install the Blackmagic DeckLink drivers to use the features of this application.");
		return false;
	}

    pDL = getDeckLink(device);

    if(mDLInput) {
        mDLInput->StopStreams();
        mDLInput->DisableVideoInput();
        mDLInput->Release();
        mDLInput = NULL;
    }

    pDL->QueryInterface(IID_IDeckLinkInput, (void**)&mDLInput);
    if (! mDLInput)
	{
        QMessageBox::critical(NULL, "Expected Input DeckLink devices", "This application requires DeckLink device.");
		goto error;
	}

    if (mDLInput->GetDisplayModeIterator(&pDLDisplayModeIterator) != S_OK)
    {
        QMessageBox::critical(NULL, "Cannot get Display Mode Iterator.", "DeckLink error.");
        goto error;
    }

    pDLDisplayMode = getDeckLinkDisplayMode(pDL, mode);
    if (pDLDisplayMode == NULL)
    {
        QMessageBox::critical(NULL, "Cannot get specified BMDDisplayMode.", "DeckLink error.");
        goto error;
    }

    displayMode = pDLDisplayMode->GetDisplayMode();

	mFrameWidth = pDLDisplayMode->GetWidth();
	mFrameHeight = pDLDisplayMode->GetHeight();

	// Compute a rotate angle rate so box will spin at a rate independent of video mode frame rate
	pDLDisplayMode->GetFrameRate(&mFrameDuration, &mFrameTimescale);
	fps = (float)mFrameTimescale / (float)mFrameDuration;
    //mRotateAngleRate = 35.0f / fps;			// rotate box through 35 degrees every second

	// resize window to match video frame, but scale large formats down by half for viewing
	if (mFrameWidth < 1920)
		mParent->resize(mFrameWidth, mFrameHeight);
	else
		mParent->resize(mFrameWidth / 2, mFrameHeight / 2);

	// Check required extensions and setup OpenGL state
    if (! InitOpenGLState())
		goto error;

	// Capture will use a user-supplied frame memory allocator
	// For large frames use a reduced allocator frame cache size to avoid out-of-memory
	mCaptureAllocator = new PinnedMemoryAllocator(this, "Capture", mFrameWidth < 1920 ? 2 : 1);

	if (mDLInput->SetVideoInputFrameMemoryAllocator(mCaptureAllocator) != S_OK)
		goto error;

	if (mDLInput->EnableVideoInput(displayMode, bmdFormat8BitYUV, bmdVideoInputFlagDefault) != S_OK)
		goto error;

	mCaptureDelegate = new CaptureDelegate();
	if (mDLInput->SetCallback(mCaptureDelegate) != S_OK)
		goto error;

	// Use signals and slots to ensure OpenGL rendering is performed on the main thread
	connect(mCaptureDelegate, SIGNAL(captureFrameArrived(IDeckLinkVideoInputFrame*, bool)), this, SLOT(VideoFrameArrived(IDeckLinkVideoInputFrame*, bool)), Qt::QueuedConnection);

	bSuccess = true;

error:
	if (!bSuccess)
	{
		if (mDLInput != NULL)
		{
			mDLInput->Release();
			mDLInput = NULL;
		}
    }

	if (pDL != NULL)
	{
		pDL->Release();
		pDL = NULL;
	}

	if (pDLIterator != NULL)
	{
		pDLIterator->Release();
		pDLIterator = NULL;
	}

	return bSuccess;
}

//
// QGLWidget virtual methods
//
void OpenGLCapture::initializeGL ()
{
	// Initialization is deferred to InitOpenGLState() when the width and height of the DeckLink video frame are known
}

void OpenGLCapture::paintGL ()
{
	// The DeckLink API provides IDeckLinkGLScreenPreviewHelper as a convenient way to view the playout video frames
	// in a window.  However, it performs a copy from host memory to the GPU which is wasteful in this case since
	// we already have the rendered frame to be played out sitting in the GPU in the mIdFrameBuf frame buffer.

	// Simply copy the off-screen frame buffer to on-screen frame buffer, scaling to the viewing window size.
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER, mIdFrameBuf);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
	glViewport(0, 0, mViewWidth, mViewHeight);
	glBlitFramebufferEXT(0, 0, mFrameWidth, mFrameHeight, 0, 0, mViewWidth, mViewHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void OpenGLCapture::resizeGL (int width, int height)
{
	// We don't set the project or model matrices here since the window data is copied directly from
	// an off-screen FBO in paintGL().  Just save the width and height for use in paintGL().
	mViewWidth = width;
	mViewHeight = height;
}

bool OpenGLCapture::InitOpenGLState()
{
	makeCurrent();

	if (! CheckOpenGLExtensions())
		return false;

	// Prepare the shader used to perform colour space conversion on the video texture
	char compilerErrorMessage[1024];
	if (! compileFragmentShader(sizeof(compilerErrorMessage), compilerErrorMessage))
	{
		QMessageBox::critical(NULL, compilerErrorMessage, "OpenGL Shader failed to compile");
		return false;
	}

	// Setup the scene
	glShadeModel( GL_SMOOTH );					// Enable smooth shading
	glClearColor( 0.0f, 0.0f, 0.0f, 0.5f );		// Black background
	glClearDepth( 1.0f );						// Depth buffer setup
	glEnable( GL_DEPTH_TEST );					// Enable depth testing
	glDepthFunc( GL_LEQUAL );					// Type of depth test to do
	glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );

	if (! mPinnedMemoryExtensionAvailable)
	{
		glGenBuffers(1, &mUnpinnedTextureBuffer);
	}

	// Setup the texture which will hold the captured video frame pixels
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &mTexture);
	glBindTexture(GL_TEXTURE_2D, mTexture);

	// Parameters to control how texels are sampled from the texture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	// Create texture with empty data, we will update it using glTexSubImage2D each frame.
	// The captured video is YCbCr 4:2:2 packed into a UYVY macropixel.  OpenGL has no YCbCr format
	// so treat it as RGBA 4:4:4:4 by halving the width and using GL_RGBA internal format.
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mFrameWidth/2, mFrameHeight, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);

	// Create Frame Buffer Object (FBO) to perform off-screen rendering of scene.
	// This allows the render to be done on a framebuffer with width and height exactly matching the video format.
	glGenFramebuffersEXT(1, &mIdFrameBuf);
	glGenRenderbuffersEXT(1, &mIdColorBuf);
	glGenRenderbuffersEXT(1, &mIdDepthBuf);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, mIdFrameBuf);

	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, mIdColorBuf);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, mFrameWidth, mFrameHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, mIdDepthBuf);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, mFrameWidth, mFrameHeight);

	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, mIdColorBuf);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, mIdDepthBuf);

	GLenum glStatus = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
	if (glStatus != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		QMessageBox::critical(NULL, "Cannot initialize framebuffer.", "OpenGL initialization error.");
		return false;
	}

    // VR
    if(m_vertices.size() < 1)
    {
        QMessageBox::critical(NULL, "No vertices", "OpenGL initialization error.");
        return false;
    }
    // create vbo
    glGenBuffers(1,&m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*m_vertices.size(), &m_vertices[0], GL_STATIC_DRAW);

    unsigned int val;
    val = glGetAttribLocation(mProgram, "position");
    glEnableVertexAttribArray(val);
    glVertexAttribPointer( val,  2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);

    val = glGetAttribLocation(mProgram, "texCoord");
    glEnableVertexAttribArray(val);
    glVertexAttribPointer( val,  3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)(2*sizeof(float)));

    // create ibo
    glGenBuffers(1,&m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int)*m_indices.size(), &m_indices[0], GL_STATIC_DRAW);


	return true;
}

//VR
void OpenGLCapture::setTextureBounds()
{
    float leftBounds[] = {0, 0, 0.5, 1};
    float rightBounds[] = {0.5, 0, 0.5, 1};

    // Left eye
    m_viewportOffsetScale[0] = leftBounds[0]; // X
    m_viewportOffsetScale[1] = leftBounds[1]; // Y
    m_viewportOffsetScale[2] = leftBounds[2]; // Width
    m_viewportOffsetScale[3] = leftBounds[3]; // Height

    // Right eye
    m_viewportOffsetScale[4] = rightBounds[0]; // X
    m_viewportOffsetScale[5] = rightBounds[1]; // Y
    m_viewportOffsetScale[6] = rightBounds[2]; // Width
    m_viewportOffsetScale[7] = rightBounds[3]; // Height
}

float lerp(float a, float b, float t) {
    return a + ((b - a) * t);
}

void OpenGLCapture::computeMeshVertices(int width, int height)
{
    m_vertices.resize(2 * width * height * 5);

    float lensFrustum[4];
    m_deviceInfo->getLeftEyeVisibleTanAngles(lensFrustum);

    float noLensFrustum[4];
    m_deviceInfo->getLeftEyeNoLensTanAngles(noLensFrustum);

    float viewport[4];
    m_deviceInfo->getLeftEyeVisibleScreenRect(noLensFrustum, viewport);
    //viewport[4]: x, y, width, height

    float vidx = 0;
    float iidx = 0;
    for (int e = 0; e < 2; e++) {
        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++, vidx++) {
                float u = 1.0 * i / (width - 1);
                float v = 1.0 * j / (height - 1);

                // Grid points regularly spaced in StreoScreen, and barrel distorted in
                // the mesh.
                float s = u;
                float t = v;
                float x = lerp(lensFrustum[0], lensFrustum[2], u);
                float y = lerp(lensFrustum[3], lensFrustum[1], v);
                float d = sqrt(x * x + y * y);
                float r = m_deviceInfo->distortInverse(d);
                float p = x * r / d;
                float q = y * r / d;
                u = (p - noLensFrustum[0]) / (noLensFrustum[2] - noLensFrustum[0]);
                v = (q - noLensFrustum[3]) / (noLensFrustum[1] - noLensFrustum[3]);

                // Convert u,v to mesh screen coordinates.
                float aspect = m_deviceInfo->getDevice().widthMeters / m_deviceInfo->getDevice().heightMeters;

                // FIXME: The original Unity plugin multiplied U by the aspect ratio
                // and didn't multiply either value by 2, but that seems to get it
                // really close to correct looking for me. I hate this kind of "Don't
                // know why it works" code though, and wold love a more logical
                // explanation of what needs to happen here.
                u = (viewport[0] + u * viewport[2] - 0.5) * 2.0; // * aspect;
                v = (viewport[1] + v * viewport[3] - 0.5) * 2.0;

                m_vertices[(vidx * 5) + 0] = u; // position.x
                m_vertices[(vidx * 5) + 1] = v; // position.y
                m_vertices[(vidx * 5) + 2] = s; // texCoord.x
                m_vertices[(vidx * 5) + 3] = t; // texCoord.y
                m_vertices[(vidx * 5) + 4] = e; // texCoord.z (viewport index)

                //cout << u << " " << v << endl;
            }
        }
        float w = lensFrustum[2] - lensFrustum[0];
        lensFrustum[0] = -(w + lensFrustum[0]);
        lensFrustum[2] = w - lensFrustum[2];
        w = noLensFrustum[2] - noLensFrustum[0];
        noLensFrustum[0] = -(w + noLensFrustum[0]);
        noLensFrustum[2] = w - noLensFrustum[2];
        viewport[0] = 1 - (viewport[0] + viewport[2]);
    }
}

void OpenGLCapture::computeMeshIndices(int width, int height)
{
    m_indices.resize(2 * (width - 1) * (height - 1) * 6);

    float halfwidth = width / 2;
    float halfheight = height / 2;
    float vidx = 0;
    float iidx = 0;
    for (int e = 0; e < 2; e++) {
        for (int j = 0; j < height; j++) {
            for (int i = 0; i < width; i++, vidx++) {
                if (i == 0 || j == 0)
                  continue;
                // Build a quad.  Lower right and upper left quadrants have quads with
                // the triangle diagonal flipped to get the vignette to interpolate
                // correctly.
                if ((i <= halfwidth) == (j <= halfheight)) {
                    // Quad diagonal lower left to upper right.
                    m_indices[iidx++] = vidx;
                    m_indices[iidx++] = vidx - width - 1;
                    m_indices[iidx++] = vidx - width;
                    m_indices[iidx++] = vidx - width - 1;
                    m_indices[iidx++] = vidx;
                    m_indices[iidx++] = vidx - 1;
                } else {
                    // Quad diagonal upper left to lower right.
                    m_indices[iidx++] = vidx - 1;
                    m_indices[iidx++] = vidx - width;
                    m_indices[iidx++] = vidx;
                    m_indices[iidx++] = vidx - width;
                    m_indices[iidx++] = vidx - 1;
                    m_indices[iidx++] = vidx - width - 1;
                }
            }
        }
    }
}



//
// Update the captured video frame texture
//
void OpenGLCapture::VideoFrameArrived(IDeckLinkVideoInputFrame* inputFrame, bool hasNoInputSource)
{
	mMutex.lock();

	mHasNoInputSource = hasNoInputSource;

	long textureSize = inputFrame->GetRowBytes() * inputFrame->GetHeight();
	void* videoPixels;
	inputFrame->GetBytes(&videoPixels);

	makeCurrent();

	glEnable(GL_TEXTURE_2D);

	if (! mPinnedMemoryExtensionAvailable)
	{
		// Use a normal texture buffer
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mUnpinnedTextureBuffer);
		glBufferData(GL_PIXEL_UNPACK_BUFFER, textureSize, videoPixels, GL_DYNAMIC_DRAW);
	}
	else
	{
		// Use a pinned buffer for the GL_PIXEL_UNPACK_BUFFER target
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mCaptureAllocator->bufferObjectForPinnedAddress(textureSize, videoPixels));
	}
	glBindTexture(GL_TEXTURE_2D, mTexture);

	// NULL for last arg indicates use current GL_PIXEL_UNPACK_BUFFER target as texture data
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mFrameWidth/2, mFrameHeight, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	if (mPinnedMemoryExtensionAvailable)
	{
		// Ensure pinned texture has been transferred to GPU before we draw with it
		GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 40 * 1000 * 1000);	// timeout in nanosec
		glDeleteSync(fence);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glDisable(GL_TEXTURE_2D);

    drawFrame();

	mMutex.unlock();
	inputFrame->Release();
}

// Draw the captured video frame texture onto a box, rendering to the off-screen frame buffer.
// Read the rendered scene back from the frame buffer and schedule it for playout.
void OpenGLCapture::drawFrame()
{
    //mMutex.lock();

	// make GL context current
	makeCurrent();

	// Draw OpenGL scene to the off-screen frame buffer
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, mIdFrameBuf);

    GLfloat aspectRatio = (GLfloat)mFrameWidth / (GLfloat)mFrameHeight;
    glViewport (0, 0, mFrameWidth, mFrameHeight);

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glScalef( aspectRatio, 1.0f, 1.0f );
    glFinish();

	if (mHasNoInputSource)
	{
		// Draw a big X when no input is available on capture
		glBegin( GL_QUADS );
		glColor3f( 1.0f, 0.0f, 1.0f );
		glVertex3f(  0.8f,  0.9f,  1.0f );
		glVertex3f(  0.9f,  0.8f,  1.0f );
		glColor3f( 1.0f, 1.0f, 0.0f );
		glVertex3f( -0.8f, -0.9f,  1.0f );
		glVertex3f( -0.9f, -0.8f,  1.0f );
		glColor3f( 1.0f, 0.0f, 1.0f );
		glVertex3f( -0.8f,  0.9f,  1.0f );
		glVertex3f( -0.9f,  0.8f,  1.0f );
		glColor3f( 1.0f, 1.0f, 0.0f );
		glVertex3f(  0.8f, -0.9f,  1.0f );
		glVertex3f(  0.9f, -0.8f,  1.0f );
		glEnd();
	}
	else
	{
		// Pass texture unit 0 to the fragment shader as a uniform variable
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, mTexture);
		glUseProgram(mProgram);
		GLint locUYVYtex = glGetUniformLocation(mProgram, "UYVYtex");
		glUniform1i(locUYVYtex, 0);		// Bind texture unit 0

        GLint locOffset = glGetUniformLocation(mProgram, "viewportOffsetScale");
        glUniform4fv(locOffset, 2, m_viewportOffsetScale);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        glDrawElements( GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, (void*)0 );

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glUseProgram(0);
		glDisable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

    //mMutex.unlock();
	updateGL();				// Trigger the QGLWidget to repaint the on-screen window in paintGL()
}

bool OpenGLCapture::Start()
{
	mDLInput->StartStreams();

	return true;
}

bool OpenGLCapture::Stop()
{
	mDLInput->StopStreams();
	mDLInput->DisableVideoInput();

	return true;
}

// Setup fragment shader to take YCbCr 4:2:2 video texture in UYVY macropixel format
// and perform colour space conversion to RGBA in the GPU.
bool OpenGLCapture::compileFragmentShader(int errorMessageSize, char* errorMessage)
{
	GLsizei		errorBufferSize;
	GLint		compileResult, linkResult;

    const char* vertexSource =
        "#version 130 \n"

        "attribute vec2 position; \n"
        "attribute vec3 texCoord; \n"

        "varying vec2 vTexCoord; \n"

        "uniform vec4 viewportOffsetScale[2]; \n"

        "void main() { \n"
        "    vec4 viewport = viewportOffsetScale[int(texCoord.z)]; \n"
        "    vTexCoord = (texCoord.xy * viewport.zw) + viewport.xy; \n"
        "    vTexCoord.y = 1 - vTexCoord.y; \n"
        "    gl_Position = vec4( position, 1.0, 1.0 ); \n"
        "} \n";

	const char*	fragmentSource =
		"#version 130 \n"
		"uniform sampler2D UYVYtex; \n"		// UYVY macropixel texture passed as RGBA format
        "varying vec2 vTexCoord; \n"

		"vec4 rec709YCbCr2rgba(float Y, float Cb, float Cr, float a) \n"
		"{ \n"
		"	float r, g, b; \n"
		// Y: Undo 1/256 texture value scaling and scale [16..235] to [0..1] range
		// C: Undo 1/256 texture value scaling and scale [16..240] to [-0.5 .. + 0.5] range
		"	Y = (Y * 256.0 - 16.0) / 219.0; \n"
		"	Cb = (Cb * 256.0 - 16.0) / 224.0 - 0.5; \n"
		"	Cr = (Cr * 256.0 - 16.0) / 224.0 - 0.5; \n"
		// Convert to RGB using Rec.709 conversion matrix (see eq 26.7 in Poynton 2003)
		"	r = Y + 1.5748 * Cr; \n"
		"	g = Y - 0.1873 * Cb - 0.4681 * Cr; \n"
		"	b = Y + 1.8556 * Cb; \n"
		"	return vec4(r, g, b, a); \n"
		"}\n"

		// Perform bilinear interpolation between the provided components.
		// The samples are expected as shown:
		// ---------
		// | X | Y |
		// |---+---|
		// | W | Z |
		// ---------
		"vec4 bilinear(vec4 W, vec4 X, vec4 Y, vec4 Z, vec2 weight) \n"
		"{\n"
		"	vec4 m0 = mix(W, Z, weight.x);\n"
		"	vec4 m1 = mix(X, Y, weight.x);\n"
		"	return mix(m0, m1, weight.y); \n"
		"}\n"

		// Gather neighboring YUV macropixels from the given texture coordinate
		"void textureGatherYUV(sampler2D UYVYsampler, vec2 tc, out vec4 W, out vec4 X, out vec4 Y, out vec4 Z) \n"
		"{\n"
		"	ivec2 tx = ivec2(tc * textureSize(UYVYsampler, 0));\n"
		"	ivec2 tmin = ivec2(0,0);\n"
		"	ivec2 tmax = textureSize(UYVYsampler, 0) - ivec2(1,1);\n"
		"	W = texelFetch(UYVYsampler, tx, 0); \n"
		"	X = texelFetch(UYVYsampler, clamp(tx + ivec2(0,1), tmin, tmax), 0); \n"
		"	Y = texelFetch(UYVYsampler, clamp(tx + ivec2(1,1), tmin, tmax), 0); \n"
		"	Z = texelFetch(UYVYsampler, clamp(tx + ivec2(1,0), tmin, tmax), 0); \n"
		"}\n"

		"void main(void) \n"
		"{\n"
		/* The shader uses texelFetch to obtain the YUV macropixels to avoid unwanted interpolation
		 * introduced by the GPU interpreting the YUV data as RGBA pixels.
		 * The YUV macropixels are converted into individual RGB pixels and bilinear interpolation is applied. */
        "	//vec2 tc = gl_TexCoord[0].st; \n"
		"	float alpha = 0.7; \n"

		"	vec4 macro, macro_u, macro_r, macro_ur;\n"
		"	vec4 pixel, pixel_r, pixel_u, pixel_ur; \n"
        "	textureGatherYUV(UYVYtex, vTexCoord, macro, macro_u, macro_ur, macro_r);\n"

		//   Select the components for the bilinear interpolation based on the texture coordinate
		//   location within the YUV macropixel:
		//   -----------------          ----------------------
		//   | UY/VY | UY/VY |          | macro_u | macro_ur |
		//   |-------|-------|    =>    |---------|----------|
		//   | UY/VY | UY/VY |          | macro   | macro_r  |
		//   |-------|-------|          ----------------------
		//   | RG/BA | RG/BA |
		//   -----------------
        "	vec2 off = fract(vTexCoord * textureSize(UYVYtex, 0)); \n"
		"	if (off.x > 0.5) { \n"			// right half of macropixel
		"		pixel = rec709YCbCr2rgba(macro.a, macro.b, macro.r, alpha); \n"
		"		pixel_r = rec709YCbCr2rgba(macro_r.g, macro_r.b, macro_r.r, alpha); \n"
		"		pixel_u = rec709YCbCr2rgba(macro_u.a, macro_u.b, macro_u.r, alpha); \n"
		"		pixel_ur = rec709YCbCr2rgba(macro_ur.g, macro_ur.b, macro_ur.r, alpha); \n"
		"	} else { \n"					// left half & center of macropixel
		"		pixel = rec709YCbCr2rgba(macro.g, macro.b, macro.r, alpha); \n"
		"		pixel_r = rec709YCbCr2rgba(macro.a, macro.b, macro.r, alpha); \n"
		"		pixel_u = rec709YCbCr2rgba(macro_u.g, macro_u.b, macro_u.r, alpha); \n"
		"		pixel_ur = rec709YCbCr2rgba(macro_u.a, macro_u.b, macro_u.r, alpha); \n"
		"	}\n"

		"	gl_FragColor = bilinear(pixel, pixel_u, pixel_ur, pixel_r, off); \n"
		"}\n";

    mVertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(mVertexShader, 1, (const GLchar**)&vertexSource, NULL);
    glCompileShader(mVertexShader);
    glGetShaderiv(mVertexShader, GL_COMPILE_STATUS, &compileResult);
    if (compileResult == GL_FALSE)
    {
        glGetShaderInfoLog(mVertexShader, errorMessageSize, &errorBufferSize, errorMessage);
        qDebug() << errorMessage;
        return false;
    }

	mFragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(mFragmentShader, 1, (const GLchar**)&fragmentSource, NULL);
	glCompileShader(mFragmentShader);
	glGetShaderiv(mFragmentShader, GL_COMPILE_STATUS, &compileResult);
	if (compileResult == GL_FALSE)
	{
		glGetShaderInfoLog(mFragmentShader, errorMessageSize, &errorBufferSize, errorMessage);
        qDebug() << errorMessage;
		return false;
	}

	mProgram = glCreateProgram();

    glAttachShader(mProgram, mVertexShader);
	glAttachShader(mProgram, mFragmentShader);
	glLinkProgram(mProgram);

	glGetProgramiv(mProgram, GL_LINK_STATUS, &linkResult);
	if (linkResult == GL_FALSE)
	{
		glGetProgramInfoLog(mProgram, errorMessageSize, &errorBufferSize, errorMessage);
        qDebug() << errorMessage;
		return false;
	}

	return true;
}

bool OpenGLCapture::CheckOpenGLExtensions()
{
	const GLubyte* strExt;
	GLboolean hasFBO, hasPinned;

	if (! isValid())
	{
		QMessageBox::critical(NULL,"OpenGL initialization error.", "OpenGL context is not valid for specified QGLFormat.");
		return false;
	}

	makeCurrent();
	strExt = glGetString (GL_EXTENSIONS);
	hasFBO = gluCheckExtension ((const GLubyte*)"GL_EXT_framebuffer_object", strExt);
	hasPinned = gluCheckExtension ((const GLubyte*)"GL_AMD_pinned_memory", strExt);

	mPinnedMemoryExtensionAvailable = hasPinned;

	if (!hasFBO)
	{
		QMessageBox::critical(NULL,"OpenGL initialization error.", "OpenGL extension \"GL_EXT_framebuffer_object\" is not supported.");
		return false;
	}

	if (!mPinnedMemoryExtensionAvailable)
		fprintf(stderr, "GL_AMD_pinned_memory extension not available, using regular texture buffer fallback instead\n");

	return true;
}

////////////////////////////////////////////
// PinnedMemoryAllocator
////////////////////////////////////////////

// PinnedMemoryAllocator implements the IDeckLinkMemoryAllocator interface and can be used instead of the
// built-in frame allocator, by setting with SetVideoInputFrameMemoryAllocator() or SetVideoOutputFrameMemoryAllocator().
//
// For this sample application a custom frame memory allocator is used to ensure each address
// of frame memory is aligned on a 4kB boundary required by the OpenGL pinned memory extension.
// If the pinned memory extension is not available, this allocator will still be used and
// demonstrates how to cache frame allocations for efficiency.
//
// The frame cache delays the releasing of buffers until the cache fills up, thereby avoiding an
// allocate plus pin operation for every frame, followed by an unpin and deallocate on every frame.

PinnedMemoryAllocator::PinnedMemoryAllocator(QGLWidget* context, const char *name, unsigned cacheSize) :
	mContext(context),
	mRefCount(1),
	mName(name),
	mFrameCacheSize(cacheSize)	// large cache size will keep more GPU memory pinned and may result in out of memory errors
{
}

PinnedMemoryAllocator::~PinnedMemoryAllocator()
{
}

GLuint PinnedMemoryAllocator::bufferObjectForPinnedAddress(int bufferSize, const void* address)
{
	// Store all input memory buffers in a map to lookup corresponding pinned buffer handle
	if (mBufferHandleForPinnedAddress.count(address) == 0)
	{
		// This method assumes the OpenGL context is current

		// Create a handle to use for pinned memory
		GLuint bufferHandle;
		glGenBuffers(1, &bufferHandle);

		// Pin memory by binding buffer to special AMD target.
		glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, bufferHandle);

		// glBufferData() sets up the address so any OpenGL operation on this buffer will use client memory directly
		// (assumes address is aligned to 4k boundary).
		glBufferData(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, bufferSize, address, GL_STREAM_DRAW);
		GLenum result = glGetError();
		if (result != GL_NO_ERROR)
		{
			fprintf(stderr, "%s allocator: Error pinning memory with glBufferData(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, size=%d ...) error=%s\n", mName, bufferSize, gluErrorString(result));
			exit(1);
		}
		glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0);		// Unbind buffer to target

		mBufferHandleForPinnedAddress[address] = bufferHandle;
	}

	return mBufferHandleForPinnedAddress[address];
}

void PinnedMemoryAllocator::unPinAddress(const void* address)
{
	// un-pin address only if it has been pinned
	if (mBufferHandleForPinnedAddress.count(address) > 0)
	{
		mContext->makeCurrent();

		// The buffer is un-pinned by the GPU when the buffer is deleted
		GLuint bufferHandle = mBufferHandleForPinnedAddress[address];
		glDeleteBuffers(1, &bufferHandle);
		mBufferHandleForPinnedAddress.erase(address);
	}
}

// IUnknown methods
HRESULT STDMETHODCALLTYPE	PinnedMemoryAllocator::QueryInterface(REFIID /*iid*/, LPVOID* /*ppv*/)
{
	return E_NOTIMPL;
}

ULONG STDMETHODCALLTYPE		PinnedMemoryAllocator::AddRef(void)
{
	int oldValue = mRefCount.fetchAndAddAcquire(1);
	return (ULONG)(oldValue + 1);
}

ULONG STDMETHODCALLTYPE		PinnedMemoryAllocator::Release(void)
{
	int oldValue = mRefCount.fetchAndAddAcquire(-1);
	if (oldValue == 1)		// i.e. current value will be 0
		delete this;

	return (ULONG)(oldValue - 1);
}

// IDeckLinkMemoryAllocator methods
HRESULT STDMETHODCALLTYPE	PinnedMemoryAllocator::AllocateBuffer (uint32_t bufferSize, void* *allocatedBuffer)
{
	if (mFrameCache.empty())
	{
		// alignment to 4K required when pinning memory
		if (posix_memalign(allocatedBuffer, 4096, bufferSize) != 0)
			return E_OUTOFMEMORY;
	}
	else
	{
		// Re-use most recently ReleaseBuffer'd address
		*allocatedBuffer = mFrameCache.back();
		mFrameCache.pop_back();
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE	PinnedMemoryAllocator::ReleaseBuffer (void* buffer)
{
	if (mFrameCache.size() < mFrameCacheSize)
	{
		mFrameCache.push_back(buffer);
	}
	else
	{
		// No room left in cache, so un-pin (if it was pinned) and free this buffer
		unPinAddress(buffer);
		free(buffer);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE	PinnedMemoryAllocator::Commit ()
{
	return S_OK;
}

HRESULT STDMETHODCALLTYPE	PinnedMemoryAllocator::Decommit ()
{
	while (! mFrameCache.empty())
	{
		// Cleanup any frames allocated and pinned in AllocateBuffer() but not freed in ReleaseBuffer()
		unPinAddress( mFrameCache.back() );
		free( mFrameCache.back() );
		mFrameCache.pop_back();
	}
	return S_OK;
}

////////////////////////////////////////////
// DeckLink Capture Delegate Class
////////////////////////////////////////////
HRESULT	CaptureDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame* inputFrame, IDeckLinkAudioInputPacket* /*audioPacket*/)
{
	if (! inputFrame)
	{
		// It's possible to receive a NULL inputFrame, but a valid audioPacket. Ignore audio-only frame.
		return S_OK;
	}

	bool hasNoInputSource = inputFrame->GetFlags() & bmdFrameHasNoInputSource;

	// emit just adds a message to Qt's event queue since we're in a different thread, so add a reference
	// to the input frame to prevent it getting released before the connected slot can process the frame.
	inputFrame->AddRef();
	emit captureFrameArrived(inputFrame, hasNoInputSource);
	return S_OK;
}

HRESULT	CaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents /*notificationEvents*/, IDeckLinkDisplayMode* /*newDisplayMode*/, BMDDetectedVideoInputFormatFlags /*detectedSignalFlags*/)
{
	fprintf(stderr, "VideoInputFormatChanged()\n");
	return S_OK;
}
