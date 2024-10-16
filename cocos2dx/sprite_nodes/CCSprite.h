/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2011      Zynga Inc.

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

#ifndef __SPITE_NODE_CCSPRITE_H__
#define __SPITE_NODE_CCSPRITE_H__

#include "base_nodes/CCNode.h"
#include "CCProtocols.h"
#include "textures/CCTextureAtlas.h"
#include "ccTypes.h"
#include "cocoa/CCDictionary.h"
#include <string>
#ifdef EMSCRIPTEN
#include "base_nodes/CCGLBufferedNode.h"
#endif // EMSCRIPTEN

NS_CC_BEGIN

class CCSpriteBatchNode;
class CCSpriteFrame;
class CCAnimation;
class CCRect;
class CCPoint;
class CCSize;
class CCTexture2D;
struct transformValues_;

/**
 * @addtogroup sprite_nodes
 * @{
 */

#define CCSpriteIndexNotInitialized 0xffffffff     /// CCSprite invalid index on the CCSpriteBatchNode


/** CCSprite is a 2d image ( http://en.wikipedia.org/wiki/Sprite_(computer_graphics) )
*
* CCSprite can be created with an image, or with a sub-rectangle of an image.
*
* If the parent or any of its ancestors is a CCSpriteBatchNode then the following features/limitations are valid
*    - Features when the parent is a CCBatchNode:
*        - MUCH faster rendering, specially if the CCSpriteBatchNode has many children. All the children will be drawn in a single batch.
*
*    - Limitations
*        - Camera is not supported yet (eg: CCOrbitCamera action doesn't work)
*        - GridBase actions are not supported (eg: CCLens, CCRipple, CCTwirl)
*        - The Alias/Antialias property belongs to CCSpriteBatchNode, so you can't individually set the aliased property.
*        - The Blending function property belongs to CCSpriteBatchNode, so you can't individually set the blending function property.
*        - Parallax scroller is not supported, but can be simulated with a "proxy" sprite.
*
*  If the parent is an standard CCNode, then CCSprite behaves like any other CCNode:
*    - It supports blending functions
*    - It supports aliasing / antialiasing
*    - But the rendering will be slower: 1 draw per children.
*
* The default anchorPoint in CCSprite is (0.5, 0.5).
*/
class CC_DLL CCSprite : public CCNode, public CCTextureProtocol, public CCRGBAProtocol
#ifdef EMSCRIPTEN
, public CCGLBufferedNode
#endif // EMSCRIPTEN
{
    /** Opacity: conforms to CCRGBAProtocol protocol */
    CC_PROPERTY_OVERRIDE(GLubyte, m_nOpacity, Opacity)
    /** Color: conforms with CCRGBAProtocol protocol */
    CC_PROPERTY_PASS_BY_REF_OVERRIDE(ccColor3B, m_sColor, Color);
public:
    virtual void draw(void) override;

public:
    // attributes

    /** whether or not the Sprite needs to be updated in the Atlas */
    inline virtual bool isDirty(void) { return m_bDirty; }
    /** make the Sprite to be updated in the Atlas. */
    inline virtual void setDirty(bool bDirty) { m_bDirty = bDirty; }

    /** get the quad (tex coords, vertex coords and color) information */
    inline ccV3F_C4B_T2F_Quad getQuad(void) { return m_sQuad; }

    /** returns whether or not the texture rectangle is rotated */
    inline bool isTextureRectRotated(void) { return m_bRectRotated; }
    
    /** Set the index used on the TextureAtlas. */
    inline unsigned int getAtlasIndex(void) { return m_uAtlasIndex; }
    /** Set the index used on the TextureAtlas.
    @warning Don't modify this value unless you know what you are doing 
    */
    inline void setAtlasIndex(unsigned int uAtlasIndex) { m_uAtlasIndex = uAtlasIndex; }

    /** returns the rect of the CCSprite in points */
    inline const CCRect& getTextureRect(void) { return m_obRect; }

    inline CCTextureAtlas* getTextureAtlas(void) { return m_pobTextureAtlas; }
    inline void setTextureAtlas(CCTextureAtlas *pobTextureAtlas) { m_pobTextureAtlas = pobTextureAtlas; }

    CCSpriteBatchNode* getSpriteBatchNode(void);
    void setSpriteBatchNode(CCSpriteBatchNode *pobSpriteBatchNode);

    /** Get offset position of the sprite. Calculated automatically by editors like Zwoptex.
     @since v0.99.0
     */
    inline const CCPoint& getOffsetPosition(void) { return m_obOffsetPosition; }

    /** conforms to CCTextureProtocol protocol */
    inline ccBlendFunc getBlendFunc(void) const override { return m_sBlendFunc; }
    /** conforms to CCTextureProtocol protocol */
    inline void setBlendFunc(ccBlendFunc blendFunc) override { m_sBlendFunc = blendFunc; }

public:
    /** Creates an sprite with a texture.
     The rect used will be the size of the texture.
     The offset will be (0,0).
     @deprecated: Please use createWithTexture(CCTexture2D*) instead. This interface will be deprecated sooner or later.
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithTexture(CCTexture2D *pTexture);

    /** Creates an sprite with a texture and a rect.
     The offset will be (0,0).
     @deprecated: Please use createWithTexture(CCTexture2D*, const CCRect&) instead, This interface will be deprecated sooner or later.
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithTexture(CCTexture2D *pTexture, const CCRect& rect);

    /** Creates an sprite with a texture.
     The rect used will be the size of the texture.
     The offset will be (0,0).
     */
    static CCSprite* createWithTexture(CCTexture2D *pTexture);

    /** Creates an sprite with a texture and a rect.
     The offset will be (0,0).
     */
    static CCSprite* createWithTexture(CCTexture2D *pTexture, const CCRect& rect);

    /** Creates an sprite with an sprite frame. 
    @deprecated: Please use createWithSpriteFrame(CCSpriteFrame*) instead. This interface will be deprecated sooner or later.
    */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithSpriteFrame(CCSpriteFrame *pSpriteFrame);

    /** Creates an sprite with an sprite frame name.
     An CCSpriteFrame will be fetched from the CCSpriteFrameCache by name.
     If the CCSpriteFrame doesn't exist it will raise an exception.
     @deprecated: Please use createWithSpriteFrameName(const char*) instead. This interface will be deprecated sooner or later.
     @since v0.9
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithSpriteFrameName(const char *pszSpriteFrameName);

    /** Creates an sprite with an sprite frame. */
    static CCSprite* createWithSpriteFrame(CCSpriteFrame *pSpriteFrame);

    /** Creates an sprite with an sprite frame name.
     An CCSpriteFrame will be fetched from the CCSpriteFrameCache by name.
     If the CCSpriteFrame doesn't exist it will raise an exception.
     @since v0.9
     */
    static CCSprite* createWithSpriteFrameName(const char *pszSpriteFrameName);

    /** Creates an sprite with an image filename.
     The rect used will be the size of the image.
     The offset will be (0,0).
     @deprecated: Please use create(const char*) instead. This interface will be deprecated sooner or later.
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithFile(const char *pszFileName);

    /** Creates an sprite with an image filename and a rect.
     The offset will be (0,0).
     @deprecated: Please use create(const char*, const CCRect&) instead. This interface will be deprecated sooner or later.
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* spriteWithFile(const char *pszFileName, const CCRect& rect);
    
    /** Creates an sprite with an image filename.
     The rect used will be the size of the image.
     The offset will be (0,0).
     */
    static CCSprite* create(const char *pszFileName);

    /** Creates an sprite with an image filename and a rect.
     The offset will be (0,0).
     */
    static CCSprite* create(const char *pszFileName, const CCRect& rect);

    /** Creates an sprite.
    @deprecated: Please use create() instead. This interface will be deprecated sooner or later.
     */
    CC_DEPRECATED_ATTRIBUTE static CCSprite* node();
    /** Creates an sprite.
     */
    static CCSprite* create();
public:
    CCSprite(void);
    virtual ~CCSprite(void);
    virtual bool init(void);

    virtual void removeChild(CCNode* pChild, bool bCleanup) override;
    virtual void removeAllChildrenWithCleanup(bool bCleanup) override;
    virtual void reorderChild(CCNode *pChild, int zOrder) override;
    virtual void addChild(CCNode *pChild) override;
    virtual void addChild(CCNode *pChild, int zOrder) override;
    virtual void addChild(CCNode *pChild, int zOrder, int tag) override;
    virtual void sortAllChildren() override;

    virtual void setDirtyRecursively(bool bValue);
    virtual void setPosition(const CCPoint& pos) override;
    virtual void setRotation(float fRotation) override;
    virtual void setSkewX(float sx) override;
    virtual void setSkewY(float sy) override;
    virtual void setScaleX(float fScaleX) override;
    virtual void setScaleY(float fScaleY) override;
    virtual void setScale(float fScale) override;
    virtual void setVertexZ(float fVertexZ) override;
    virtual void setAnchorPoint(const CCPoint& anchor) override;
    virtual void ignoreAnchorPointForPosition(bool value);
    virtual void setVisible(bool bVisible) override;
    /* BPC PATCH - made setFlip virtual */
    virtual void setFlipX(bool bFlipX);
    virtual void setFlipY(bool bFlipY);
    /* end BPC PATCH */
    
    /** whether or not the sprite is flipped horizontally. 
    It only flips the texture of the sprite, and not the texture of the sprite's children.
    Also, flipping the texture doesn't alter the anchorPoint.
    If you want to flip the anchorPoint too, and/or to flip the children too use:

    sprite->setScaleX(sprite->getScaleX() * -1);
    */
    bool isFlipX(void);
    /** whether or not the sprite is flipped vertically.
    It only flips the texture of the sprite, and not the texture of the sprite's children.
    Also, flipping the texture doesn't alter the anchorPoint.
    If you want to flip the anchorPoint too, and/or to flip the children too use:

    sprite->setScaleY(sprite->getScaleY() * -1);
    */
    bool isFlipY(void);

    void updateColor(void);
    // RGBAProtocol
    /** opacity: conforms to CCRGBAProtocol protocol */
    virtual void setOpacityModifyRGB(bool bValue) override;
    virtual bool isOpacityModifyRGB(void) const override;

    // CCTextureProtocol
    virtual void setTexture(CCTexture2D *texture) override;
    virtual CCTexture2D* getTexture(void) const override;

    /** Initializes an sprite with a texture.
     The rect used will be the size of the texture.
     The offset will be (0,0).
     */
    virtual bool initWithTexture(CCTexture2D *pTexture);

    /** Initializes an sprite with a texture and a rect.
     The offset will be (0,0).
     */
    virtual bool initWithTexture(CCTexture2D *pTexture, const CCRect& rect);

    /** Initializes an sprite with a texture and a rect in points, optionally rotated.
     The offset will be (0,0).
     IMPORTANT: This is the designated initializer.
     */
    virtual bool initWithTexture(CCTexture2D *pTexture, const CCRect& rect, bool rotated);

    // Initializes an sprite with an sprite frame.
    virtual bool initWithSpriteFrame(CCSpriteFrame *pSpriteFrame);

    /** Initializes an sprite with an sprite frame name.
     An CCSpriteFrame will be fetched from the CCSpriteFrameCache by name.
     If the CCSpriteFrame doesn't exist it will raise an exception.
     @since v0.9
     */
    virtual bool initWithSpriteFrameName(const char *pszSpriteFrameName);

    /** Initializes an sprite with an image filename.
     The rect used will be the size of the image.
     The offset will be (0,0).
     */
    virtual bool initWithFile(const char *pszFilename);

    /** Initializes an sprite with an image filename, and a rect.
     The offset will be (0,0).
     */
    virtual bool initWithFile(const char *pszFilename, const CCRect& rect);

    // BatchNode methods

    /** updates the quad according the the rotation, position, scale values. */
    virtual void updateTransform(void) override;

    /** updates the texture rect of the CCSprite in points. 
    It will call setTextureRect:rotated:untrimmedSize with rotated = NO, and utrimmedSize = rect.size.
    */
     virtual void setTextureRect(const CCRect& rect);

     /** set the texture rect, rectRotated and untrimmed size of the CCSprite in points.
     It will update the texture coordinates and the vertex rectangle.
     */
     virtual void setTextureRect(const CCRect& rect, bool rotated, const CCSize& untrimmedSize);

    /** set the vertex rect.
     It will be called internally by setTextureRect. Useful if you want to create 2x images from SD images in Retina Display.
     Do not call it manually. Use setTextureRect instead.
     */
     virtual void setVertexRect(const CCRect& rect);

    // Frames

    /** sets a new display frame to the CCSprite. */
    virtual void setDisplayFrame(CCSpriteFrame *pNewFrame);

    /** returns whether or not a CCSpriteFrame is being displayed */
    virtual bool isFrameDisplayed(CCSpriteFrame *pFrame);

    /** returns the current displayed frame. */
    virtual CCSpriteFrame* displayFrame(void);

    virtual CCSpriteBatchNode* getBatchNode(void);
    virtual void setBatchNode(CCSpriteBatchNode *pobSpriteBatchNode);

    // Animation

    /** changes the display frame with animation name and index.
    The animation name will be get from the CCAnimationCache
    @since v0.99.5
    */
    virtual void setDisplayFrameWithAnimationName(const char *animationName, int frameIndex);

    // BPC PATCH START -
#if (defined(COCOS2D_DEBUG) && (COCOS2D_DEBUG > 0)) || DEBUG
    virtual std::vector<const CCObject*> getSharedResources() override;
#endif
    // BPC PATCH END

protected:
    virtual void setTextureCoords(CCRect rect);
    virtual void updateBlendFunc(void);
    virtual void setReorderChildDirtyRecursively(void);

protected:
    //
    // Data used when the sprite is rendered using a CCSpriteSheet
    //
    CCTextureAtlas*     m_pobTextureAtlas;        // Sprite Sheet texture atlas (weak reference)
    unsigned int        m_uAtlasIndex;            // Absolute (real) Index on the SpriteSheet
    CCSpriteBatchNode*  m_pobBatchNode;        // Used batch node (weak reference)
    
    bool                m_bDirty;                // Sprite needs to be updated
    bool                m_bRecursiveDirty;        // Subchildren needs to be updated
    bool                m_bHasChildren;            // optimization to check if it contain children
    bool                m_bShouldBeHidden;        // should not be drawn because one of the ancestors is not visible
    CCAffineTransform   m_transformToBatch;        //
    
    //
    // Data used when the sprite is self-rendered
    //
    ccBlendFunc        m_sBlendFunc;    // Needed for the texture protocol
    CCTexture2D*       m_pobTexture;// Texture used to render the sprite

    //
    // Shared data
    //

    // texture
    CCRect m_obRect;
    bool   m_bRectRotated;

    // Offset Position (used by Zwoptex)
    CCPoint m_obOffsetPosition;
    CCPoint m_obUnflippedOffsetPositionFromCenter;

    // vertex coords, texture coords and color info
    ccV3F_C4B_T2F_Quad m_sQuad;

    // opacity and RGB protocol
    ccColor3B m_sColorUnmodified;
    bool m_bOpacityModifyRGB;

    // image is flipped
    bool m_bFlipX;
    bool m_bFlipY;
};


// end of sprite_nodes group
/// @}

NS_CC_END

#endif // __SPITE_NODE_CCSPRITE_H__
