/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Ricardo Quesada
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

#include "ccGLStateCache.h"
#include "CCGLProgram.h"
#include "CCDirector.h"
#include "ccConfig.h"

// extern
#include "kazmath/GL/matrix.h"
#include "kazmath/kazmath.h"

NS_CC_BEGIN

static GLuint    s_uCurrentProjectionMatrix = -1;
static bool        s_bVertexAttribPosition = false;
static bool        s_bVertexAttribColor = false;
static bool        s_bVertexAttribTexCoords = false;
static bool        s_bVertexAttribNormal = false;


#if CC_ENABLE_GL_STATE_CACHE

#define kCCMaxActiveTexture 16

static GLuint    s_uCurrentShaderProgram = -1;
static GLuint    s_uCurrentBoundTexture[kCCMaxActiveTexture] =  {(GLuint)-1,(GLuint)-1,(GLuint)-1,(GLuint)-1, (GLuint)-1,(GLuint)-1,(GLuint)-1,(GLuint)-1, (GLuint)-1,(GLuint)-1,(GLuint)-1,(GLuint)-1, (GLuint)-1,(GLuint)-1,(GLuint)-1,(GLuint)-1, };
static GLenum    s_eCurrentActiveTexture = (GL_TEXTURE0 - GL_TEXTURE0);
static GLenum    s_eBlendingSource = -1;
static GLenum    s_eBlendingDest = -1;
static int      s_eGLServerState = 0;

#endif // CC_ENABLE_GL_STATE_CACHE

// GL State Cache functions

void ccGLInvalidateStateCache( void )
{
    kmGLFreeAll();
    s_uCurrentProjectionMatrix = -1;
    s_bVertexAttribPosition = false;
    s_bVertexAttribColor = false;
    s_bVertexAttribTexCoords = false;
    s_bVertexAttribNormal = false;
#if CC_ENABLE_GL_STATE_CACHE
    s_uCurrentShaderProgram = -1;
    for( int i=0; i < kCCMaxActiveTexture; i++ )
    {
        s_uCurrentBoundTexture[i] = -1;
    }
    s_eCurrentActiveTexture = (GL_TEXTURE0 - GL_TEXTURE0);
    s_eBlendingSource = -1;
    s_eBlendingDest = -1;
    s_eGLServerState = 0;
#endif
}

void ccGLDeleteProgram( GLuint program )
{
#if CC_ENABLE_GL_STATE_CACHE
    if( program == s_uCurrentShaderProgram )
        s_uCurrentShaderProgram = -1;
#endif // CC_ENABLE_GL_STATE_CACHE

    glDeleteProgram( program );
}

void ccGLUseProgram( GLuint program )
{
#if CC_ENABLE_GL_STATE_CACHE
    if( program != s_uCurrentShaderProgram ) {
        s_uCurrentShaderProgram = program;
        glUseProgram(program);
    }
#else
    glUseProgram(program);
#endif // CC_ENABLE_GL_STATE_CACHE
}


void ccGLBlendFunc(GLenum sfactor, GLenum dfactor)
{
#if CC_ENABLE_GL_STATE_CACHE
    if( sfactor != s_eBlendingSource || dfactor != s_eBlendingDest ) {
        s_eBlendingSource = sfactor;
        s_eBlendingDest = dfactor;
        glBlendFunc( sfactor, dfactor );
    }
#else
    glBlendFunc( sfactor, dfactor );
#endif // CC_ENABLE_GL_STATE_CACHE
}

GLenum ccGLGetActiveTexture( void )
{
#if CC_ENABLE_GL_STATE_CACHE
    return s_eCurrentActiveTexture + GL_TEXTURE0;
#else
    GLenum activeTexture;
    glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&activeTexture);
    return activeTexture;
#endif
}

void ccGLActiveTexture( GLenum textureEnum )
{
#if CC_ENABLE_GL_STATE_CACHE
    CCAssert( (textureEnum - GL_TEXTURE0) < kCCMaxActiveTexture, "cocos2d ERROR: Increase kCCMaxActiveTexture to kCCMaxActiveTexture!");
    if( (textureEnum - GL_TEXTURE0) != s_eCurrentActiveTexture ) {
        s_eCurrentActiveTexture = (textureEnum - GL_TEXTURE0);
        glActiveTexture( textureEnum );
    }
#else
    glActiveTexture( textureEnum );
#endif
}

void ccGLBindTexture2D( GLuint textureId )
{
#if CC_ENABLE_GL_STATE_CACHE
    if( s_uCurrentBoundTexture[ s_eCurrentActiveTexture ] != textureId )
    {
        s_uCurrentBoundTexture[ s_eCurrentActiveTexture ] = textureId;
        glBindTexture(GL_TEXTURE_2D, textureId );
    }
#else
    glBindTexture(GL_TEXTURE_2D, textureId );
#endif
}


void ccGLDeleteTexture( GLuint textureId )
{
#if CC_ENABLE_GL_STATE_CACHE
    if( textureId == s_uCurrentBoundTexture[ s_eCurrentActiveTexture ] )
       s_uCurrentBoundTexture[ s_eCurrentActiveTexture ] = -1;
#endif
    glDeleteTextures(1, &textureId );
}

void ccGLEnable( ccGLServerState flags )
{
#if CC_ENABLE_GL_STATE_CACHE

    int enabled = 0;

    /* GL_BLEND */
    if( (enabled = (flags & CC_GL_BLEND)) != (s_eGLServerState & CC_GL_BLEND) ) {
        if( enabled ) {
            glEnable( GL_BLEND );
            s_eGLServerState |= CC_GL_BLEND;
        } else {
            glDisable( GL_BLEND );
            s_eGLServerState &=  ~CC_GL_BLEND;
        }
    }

#else
    if( flags & CC_GL_BLEND )
        glEnable( GL_BLEND );
    else
        glDisable( GL_BLEND );
#endif
}

//#pragma mark - GL Vertex Attrib functions

void ccGLEnableVertexAttribs( unsigned int flags )
{
    /* Position */
    bool enablePosition = flags & kCCVertexAttribFlag_Position;

    if( enablePosition != s_bVertexAttribPosition ) {
        if( enablePosition )
            glEnableVertexAttribArray( kCCVertexAttrib_Position );
        else
            glDisableVertexAttribArray( kCCVertexAttrib_Position );

        s_bVertexAttribPosition = enablePosition;
    }

    /* Color */
    bool enableColor = (flags & kCCVertexAttribFlag_Color) != 0 ? true : false;

    if( enableColor != s_bVertexAttribColor ) {
        if( enableColor )
            glEnableVertexAttribArray( kCCVertexAttrib_Color );
        else
            glDisableVertexAttribArray( kCCVertexAttrib_Color );

        s_bVertexAttribColor = enableColor;
    }

    /* Tex Coords */
    bool enableTexCoords = (flags & kCCVertexAttribFlag_TexCoords) != 0 ? true : false;

    if( enableTexCoords != s_bVertexAttribTexCoords ) {
        if( enableTexCoords )
            glEnableVertexAttribArray( kCCVertexAttrib_TexCoords );
        else
            glDisableVertexAttribArray( kCCVertexAttrib_TexCoords );

        s_bVertexAttribTexCoords = enableTexCoords;
    }

    /* Normal */
    bool enableNormal = (flags & kCCVertexAttribFlag_Normal) != 0 ? true : false;

    if( enableNormal != s_bVertexAttribNormal ) {
        if( enableNormal )
            glEnableVertexAttribArray( kCCVertexAttrib_Normal );
        else
            glDisableVertexAttribArray( kCCVertexAttrib_Normal );

        s_bVertexAttribNormal = enableNormal;
    }
}

//#pragma mark - GL Uniforms functions

void ccSetProjectionMatrixDirty( void )
{
    s_uCurrentProjectionMatrix = -1;
}

NS_CC_END
