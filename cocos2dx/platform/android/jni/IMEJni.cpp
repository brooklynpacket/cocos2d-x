/****************************************************************************
Copyright (c) 2010 cocos2d-x.org

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/
#include "IMEJni.h"
#include "text_input_node/CCIMEDispatcher.h"
#include "JniHelper.h"

#include <android/log.h>
#include <string.h>
#include <jni.h>

using namespace cocos2d;

extern "C" {
    void setKeyboardStateJNI(int bOpen) {
        if (bOpen) {
            openKeyboardJNI();
        } else {
            closeKeyboardJNI();
        }
    }

    void openKeyboardJNI() {
        JniMethodInfo t;

        if (JniHelper::getStaticMethodInfo(t, "org/cocos2dx/lib/Cocos2dxGLSurfaceView", "openIMEKeyboard", "()V")) {
            t.env->CallStaticVoidMethod(t.classID, t.methodID);
            t.env->DeleteLocalRef(t.classID);
        }
    }

    void closeKeyboardJNI() {
        JniMethodInfo t;

        if (JniHelper::getStaticMethodInfo(t, "org/cocos2dx/lib/Cocos2dxGLSurfaceView", "closeIMEKeyboard", "()V")) {
            t.env->CallStaticVoidMethod(t.classID, t.methodID);
            t.env->DeleteLocalRef(t.classID);
        }
    }

    extern void setSecureTextEntryJNI(bool bSecure)
    {
        JniMethodInfo t;

        if (JniHelper::getStaticMethodInfo(t, "org/cocos2dx/lib/Cocos2dxGLSurfaceView", "setSecureTextEntry", "(I)V")) {
            t.env->CallStaticVoidMethod(t.classID, t.methodID, (int)bSecure);
            t.env->DeleteLocalRef(t.classID);
        }
    }

    extern void setKeyboardTypeJNI(int keyboardType)
    {
        //the java code interprets the passed in int as a CCTextFieldTTF::KeyboardType
        JniMethodInfo t;
        
        if (JniHelper::getStaticMethodInfo(t, "org/cocos2dx/lib/Cocos2dxGLSurfaceView", "setKeyboardType", "(I)V")) {
            t.env->CallStaticVoidMethod(t.classID, t.methodID, (int)keyboardType);
            t.env->DeleteLocalRef(t.classID);
        }
    }
}
