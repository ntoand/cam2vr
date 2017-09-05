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


#ifndef __LOOP_THROUGH_WITH_OPENGL_COMPOSITING_H__
#define __LOOP_THROUGH_WITH_OPENGL_COMPOSITING_H__

#include "DeckLinkAPI.h"

#include <QDialog>
#include <QAction>
#include <QMainWindow>

#include <vector>
#include <string>

class OpenGLCapture;

class Cam2VR : public QMainWindow
{
public:
    Cam2VR();
    ~Cam2VR();

	void start();

protected:
    void keyPressEvent(QKeyEvent *event);

private slots:
    void captureSelectDevice();
    void captureSelectMode();
    void captureStart();
    void captureStop();
    void goFullScreen0();
    void goFullScreen1();

private:
    void createActions();
    void createMenus();
    void updateTitle();

private:
    OpenGLCapture*	pOpenGLCapture;

    //capture
    int m_device;
    int m_mode;
    std::vector<std::string> m_deviceList;
    std::vector<std::string> m_modeList;

    //gui
    QMenu *captureMenu;
    QAction *captureSelectDeviceAct;
    QAction *captureSelectModeAct;
    QAction *captureStartAct;
    QAction *captureStopAct;

    QMenu *showMenu;
    QAction *fullscreenAct0;
    QAction *fullscreenAct1;
};

#endif // __LOOP_THROUGH_WITH_OPENGL_COMPOSITING_H__
