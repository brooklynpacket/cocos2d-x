/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2008-2010 Ricardo Quesada

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

#ifndef __CCPROTOCOLS_H__
#define __CCPROTOCOLS_H__

#include "ccTypes.h"
#include "textures/CCTexture2D.h"
#include <string>

NS_CC_BEGIN

//! @brief  CC RGBA protocol
class CC_DLL CCRGBAProtocol
{
public:
    /** sets Color
     @since v0.8
     */
    virtual void setColor(const ccColor3B& color) = 0;

    /** returns the color
     @since v0.8
     */
    virtual const ccColor3B& getColor(void) const = 0;

    // returns the opacity
    virtual GLubyte getOpacity(void) const = 0;

    /** sets the opacity.
     @warning If the the texture has premultiplied alpha then, the R, G and B channels will be modified.
     Values goes from 0 to 255, where 255 means fully opaque.
     */
    virtual void setOpacity(GLubyte opacity) = 0;

    // optional

    /** sets the premultipliedAlphaOpacity property.
     If set to NO then opacity will be applied as: glColor(R,G,B,opacity);
     If set to YES then opacity will be applied as: glColor(opacity, opacity, opacity, opacity );
     Textures with premultiplied alpha will have this property by default on YES. Otherwise the default value is NO
     @since v0.8
     */
    virtual void setOpacityModifyRGB(bool bValue) = 0;

    /** returns whether or not the opacity will be applied using glColor(R,G,B,opacity) or glColor(opacity, opacity, opacity, opacity);
     @since v0.8
     */
    virtual bool isOpacityModifyRGB(void) const = 0;
};

/**
 @brief You can specify the blending function.
 @since v0.99.0
 */
class CC_DLL CCBlendProtocol
{
public:
    // set the source blending function for the texture
    virtual void setBlendFunc(ccBlendFunc blendFunc) = 0;

    // returns the blending function used for the texture
    virtual ccBlendFunc getBlendFunc(void) const = 0;
};

/** @brief CCNode objects that uses a Texture2D to render the images.
 The texture can have a blending function.
 If the texture has alpha premultiplied the default blending function is:
    src=GL_ONE dst= GL_ONE_MINUS_SRC_ALPHA
 else
    src=GL_SRC_ALPHA dst= GL_ONE_MINUS_SRC_ALPHA
 But you can change the blending function at any time.
 @since v0.8.0
 */
class CC_DLL CCTextureProtocol : public CCBlendProtocol
{
public:
    // returns the used texture
    virtual CCTexture2D* getTexture(void) const = 0;

    // sets a new texture. it will be retained
    virtual void setTexture(CCTexture2D *texture) = 0;
};

//! @brief Common interface for Labels
class CC_DLL CCLabelProtocol
{
public:
    // sets a new label using an string
    virtual void setString(const char *label) = 0;

    /** returns the string that is rendered */
    virtual const char* getString(void) = 0;
};

/** OpenGL projection protocol */
class CC_DLL CCDirectorDelegate
{
public:
    /** Called by CCDirector when the projection is updated, and "custom" projection is used
    @since v0.99.5
    */
    virtual void updateProjection(void) = 0;
};

NS_CC_END

#endif // __CCPROTOCOLS_H__
