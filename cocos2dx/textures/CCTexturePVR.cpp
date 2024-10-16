/****************************************************************************
Copyright (c) 2011        Jirka Fajfr for cocos2d-x
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2008      Apple Inc. All Rights Reserved.

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

#include "CCTexture2D.h"
#include "CCTexturePVR.h"
#include "ccMacros.h"
#include "CCConfiguration.h"
#include "support/ccUtils.h"
#include "CCStdC.h"
#include "platform/CCFileUtils.h"
#include "support/zip_support/ZipUtils.h"
#include "shaders/ccGLStateCache.h"
#include <ctype.h>
#include <cctype>
#include "BPCRetry.h"
NS_CC_BEGIN

#define PVR_TEXTURE_FLAG_TYPE_MASK    0xff

// Values taken from PVRTexture.h from http://www.imgtec.com
enum {
    kPVRTextureFlagMipmap         = (1<<8),        // has mip map levels
    kPVRTextureFlagTwiddle        = (1<<9),        // is twiddled
    kPVRTextureFlagBumpmap        = (1<<10),       // has normals encoded for a bump map
    kPVRTextureFlagTiling         = (1<<11),       // is bordered for tiled pvr
    kPVRTextureFlagCubemap        = (1<<12),       // is a cubemap/skybox
    kPVRTextureFlagFalseMipCol    = (1<<13),       // are there false colored MIP levels
    kPVRTextureFlagVolume         = (1<<14),       // is this a volume texture
    kPVRTextureFlagAlpha          = (1<<15),       // v2.1 is there transparency info in the texture
    kPVRTextureFlagVerticalFlip   = (1<<16),       // v2.1 is the texture vertically flipped
};
    
static char gPVRTexIdentifier[5] = "PVR!";

enum
{
	kPVRTexturePixelTypeRGBA_4444= 0x10,
	kPVRTexturePixelTypeRGBA_5551,
	kPVRTexturePixelTypeRGBA_8888,
	kPVRTexturePixelTypeRGB_565,
	kPVRTexturePixelTypeRGB_555,				// unsupported
	kPVRTexturePixelTypeRGB_888,
	kPVRTexturePixelTypeI_8,
	kPVRTexturePixelTypeAI_88,
	kPVRTexturePixelTypePVRTC_2,
	kPVRTexturePixelTypePVRTC_4,
	kPVRTexturePixelTypeBGRA_8888,
	kPVRTexturePixelTypeA_8,
};

static const unsigned int tableFormats[][7] = {
    
	// - PVR texture format
	// - OpenGL internal format
	// - OpenGL format
	// - OpenGL type
	// - bpp
	// - compressed
	// - Cocos2d texture format constant
	{ kPVRTexturePixelTypeRGBA_4444, GL_RGBA,	GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4,			16, false, kCCTexture2DPixelFormat_RGBA4444	},
	{ kPVRTexturePixelTypeRGBA_5551, GL_RGBA,	GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1,			16, false, kCCTexture2DPixelFormat_RGB5A1	},
	{ kPVRTexturePixelTypeRGBA_8888, GL_RGBA,	GL_RGBA, GL_UNSIGNED_BYTE,					32, false, kCCTexture2DPixelFormat_RGBA8888	},
	{ kPVRTexturePixelTypeRGB_565,	 GL_RGB,	GL_RGB,	 GL_UNSIGNED_SHORT_5_6_5,			16, false, kCCTexture2DPixelFormat_RGB565	},
	{ kPVRTexturePixelTypeRGB_888,	 GL_RGB,	GL_RGB,	 GL_UNSIGNED_BYTE,					24, false,	kCCTexture2DPixelFormat_RGB888	},
	{ kPVRTexturePixelTypeA_8,		 GL_ALPHA,	GL_ALPHA,	GL_UNSIGNED_BYTE,				8,	false, kCCTexture2DPixelFormat_A8		},
	{ kPVRTexturePixelTypeI_8,		 GL_LUMINANCE,	GL_LUMINANCE,	GL_UNSIGNED_BYTE,		8,	false, kCCTexture2DPixelFormat_I8		},
	{ kPVRTexturePixelTypeAI_88,	 GL_LUMINANCE_ALPHA,	GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,16,	false, kCCTexture2DPixelFormat_AI88	},
    // BPC PATCH:
    // By default these two are marked IOS only in Cocos (i.e. include within the below define).
    // We can handle these on android though.
	{ kPVRTexturePixelTypePVRTC_2,	 GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG, (unsigned int)-1, (unsigned int)-1,			2,	true, kCCTexture2DPixelFormat_PVRTC2	},
	{ kPVRTexturePixelTypePVRTC_4,	 GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, (unsigned int)-1, (unsigned int)-1,			4,	true, kCCTexture2DPixelFormat_PVRTC4	},
    
#if (CC_TARGET_PLATFORM == CC_PLATFORM_IOS)
    // BPC PATCH: END
	{ kPVRTexturePixelTypeBGRA_8888, GL_RGBA,	GL_BGRA, GL_UNSIGNED_BYTE,					32,	false, kCCTexture2DPixelFormat_RGBA8888	},
#endif // iphone only
};

//Tells How large is tableFormats
#define MAX_TABLE_ELEMENTS (sizeof(tableFormats) / sizeof(tableFormats[0]))

enum {
    kCCInternalPVRTextureFormat,
    kCCInternalOpenGLInternalFormat,
    kCCInternalOpenGLFormat,
    kCCInternalOpenGLType,
    kCCInternalBPP,
    kCCInternalCompressedImage,
    kCCInternalCCTexture2DPixelFormat,
};

typedef struct _PVRTexHeader
{
    unsigned int headerLength;
    unsigned int height;
    unsigned int width;
    unsigned int numMipmaps;
    unsigned int flags;
    unsigned int dataLength;
    unsigned int bpp;
    unsigned int bitmaskRed;
    unsigned int bitmaskGreen;
    unsigned int bitmaskBlue;
    unsigned int bitmaskAlpha;
    unsigned int pvrTag;
    unsigned int numSurfs;
} PVRTexHeader;

CCTexturePVR::CCTexturePVR() 
: m_uTableFormatIndex(0)
, m_uNumberOfMipmaps(0)
, m_pvrdata(0)
, m_uWidth(0)
, m_uHeight(0)
, m_bRetainName(false)
, m_bHasAlpha(false)
, m_uName(0)
, m_eFormat(kCCTexture2DPixelFormat_Default)
{
}

CCTexturePVR::~CCTexturePVR()
{
    CCLOGINFO( "cocos2d: deallocing CCTexturePVR" );

    if (m_uName != 0 && ! m_bRetainName)
    {
        ccGLDeleteTexture(m_uName);
    }

    /* Clean up any remaining PVR data. */
    deleteData();
}

bool CCTexturePVR::unpackPVRData(unsigned char* data, unsigned int len)
{
    bool success = false;
    PVRTexHeader *header = NULL;
    unsigned int flags, pvrTag;
    unsigned int dataLength = 0, dataOffset = 0, dataSize = 0;
    unsigned int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
    unsigned int width = 0, height = 0, bpp = 4;
    unsigned char *bytes = NULL;
    unsigned int formatFlags;

    //Cast first sizeof(PVRTexHeader) bytes of data stream as PVRTexHeader
    header = (PVRTexHeader *)data;

    //Make sure that tag is in correct formatting
    pvrTag = CC_SWAP_INT32_LITTLE_TO_HOST(header->pvrTag);

    /*
        Check that given data really represents pvrtexture

        [0] = 'P'
        [1] = 'V'
        [2] = 'R'
        [3] = '!'
    */
    if (gPVRTexIdentifier[0] != ((pvrTag >>  0) & 0xff) ||
        gPVRTexIdentifier[1] != ((pvrTag >>  8) & 0xff) ||
        gPVRTexIdentifier[2] != ((pvrTag >> 16) & 0xff) ||
        gPVRTexIdentifier[3] != ((pvrTag >> 24) & 0xff))
    {
        CCLOG("Unsupported PVR format. Use the Legacy format until the new format is supported");
        return false;
    }
    
    CCConfiguration *configuration = CCConfiguration::sharedConfiguration();

    flags = CC_SWAP_INT32_LITTLE_TO_HOST(header->flags);
    formatFlags = flags & PVR_TEXTURE_FLAG_TYPE_MASK;
    bool flipped = (flags & kPVRTextureFlagVerticalFlip) ? true : false;
    if (flipped)
    {
        CCLOG("cocos2d: WARNING: Image is flipped. Regenerate it using PVRTexTool");
    }

    if (! configuration->supportsNPOT() &&
        (header->width != ccNextPOT(header->width) || header->height != ccNextPOT(header->height)))
    {
        CCLOG("cocos2d: ERROR: Loading an NPOT texture (%dx%d) but is not supported on this device", header->width, header->height);
        return false;
    }

    for (m_uTableFormatIndex = 0; m_uTableFormatIndex < (unsigned int)MAX_TABLE_ELEMENTS; m_uTableFormatIndex++)
    {
        //Does image format in table fits to the one parsed from header?
        if (tableFormats[m_uTableFormatIndex][kCCInternalPVRTextureFormat] == formatFlags)
        {
            //Reset num of mipmaps
            m_uNumberOfMipmaps = 0;

            //Get size of mipmap
            m_uWidth = width = CC_SWAP_INT32_LITTLE_TO_HOST(header->width);
            m_uHeight = height = CC_SWAP_INT32_LITTLE_TO_HOST(header->height);
            
            //Do we use alpha ?
            if (CC_SWAP_INT32_LITTLE_TO_HOST(header->bitmaskAlpha))
            {
                m_bHasAlpha = true;
            }
            else
            {
                m_bHasAlpha = false;
            }
            
            //Get ptr to where data starts..
            dataLength = CC_SWAP_INT32_LITTLE_TO_HOST(header->dataLength);

            //Move by size of header
            bytes = ((unsigned char *)data) + sizeof(PVRTexHeader);
            m_eFormat = (CCTexture2DPixelFormat)(tableFormats[m_uTableFormatIndex][kCCInternalCCTexture2DPixelFormat]);
            bpp = tableFormats[m_uTableFormatIndex][kCCInternalBPP];
            
            // Calculate the data size for each texture level and respect the minimum number of blocks
            while (dataOffset < dataLength)
            {
                switch (formatFlags) {
                    case kPVRTexturePixelTypePVRTC_2:
                        blockSize = 8 * 4; // Pixel by pixel block size for 2bpp
                        widthBlocks = width / 8;
                        heightBlocks = height / 4;
                        break;
                    case kPVRTexturePixelTypePVRTC_4:
                        blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
                        widthBlocks = width / 4;
                        heightBlocks = height / 4;
                        break;
                    case kPVRTexturePixelTypeBGRA_8888:
                        if (CCConfiguration::sharedConfiguration()->supportsBGRA8888() == false) 
                        {
                            CCLOG("cocos2d: TexturePVR. BGRA8888 not supported on this device");
                            return false;
                        }
                    default:
                        blockSize = 1;
                        widthBlocks = width;
                        heightBlocks = height;
                        break;
                }
                
                // Clamp to minimum number of blocks
                if (widthBlocks < 2)
                {
                    widthBlocks = 2;
                }
                if (heightBlocks < 2)
                {
                    heightBlocks = 2;
                }

                dataSize = widthBlocks * heightBlocks * ((blockSize  * bpp) / 8);
                unsigned int packetLength = (dataLength - dataOffset);
                packetLength = packetLength > dataSize ? dataSize : packetLength;
                
                //Make record to the mipmaps array and increment counter
                m_asMipmaps[m_uNumberOfMipmaps].address = bytes + dataOffset;
                m_asMipmaps[m_uNumberOfMipmaps].len = packetLength;
                m_uNumberOfMipmaps++;
                
                //Check that we didn't overflow
                CCAssert(m_uNumberOfMipmaps < CC_PVRMIPMAP_MAX, 
                         "TexturePVR: Maximum number of mipmaps reached. Increase the CC_PVRMIPMAP_MAX value");
                
                dataOffset += packetLength;
                
                //Update width and height to the next lower power of two 
                width = MAX(width >> 1, 1);
                height = MAX(height >> 1, 1);
            }
            
            //Mark pass as success
            success = true;
            break;
        }
    }

    if (! success)
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%2x. Re-encode it with a OpenGL pixel format variant", formatFlags);
    }
    
    return success;
}

bool CCTexturePVR::createGLTexture()
{
    unsigned int width = m_uWidth;
    unsigned int height = m_uHeight;
    GLenum err;
    
    if (m_uNumberOfMipmaps > 0)
    {
        if (m_uName != 0)
        {
            ccGLDeleteTexture(m_uName);
        }
        
        // From PVR sources: "PVR files are never row aligned."
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        
        glGenTextures(1, &m_uName);
        ccGLBindTexture2D(m_uName);
        
        // Default: Anti alias.
		if (m_uNumberOfMipmaps == 1)
        {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
		else
        {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
        }
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    CHECK_GL_ERROR_DEBUG(); // clean possible GL error
    
	GLenum internalFormat = tableFormats[m_uTableFormatIndex][kCCInternalOpenGLInternalFormat];
	GLenum format = tableFormats[m_uTableFormatIndex][kCCInternalOpenGLFormat];
	GLenum type = tableFormats[m_uTableFormatIndex][kCCInternalOpenGLType];
    bool compressed = (0 == tableFormats[m_uTableFormatIndex][kCCInternalCompressedImage]) ? false : true;

    // Generate textures with mipmaps
    for (unsigned int i = 0; i < m_uNumberOfMipmaps; ++i)
    {
        if (compressed && ! CCConfiguration::sharedConfiguration()->supportsPVRTC()) 
        {
			CCLOG("cocos2d: WARNING: PVRTC images are not supported");
			return false;
		}
        
		unsigned char *data = m_asMipmaps[i].address;
		unsigned int datalen = m_asMipmaps[i].len;
        
		if (compressed)
        {
			bpcRetry(glCompressedTexImage2D(GL_TEXTURE_2D, i, internalFormat, width, height, 0, datalen, data));
        }
		else
        {
			bpcRetry(glTexImage2D(GL_TEXTURE_2D, i, internalFormat, width, height, 0, format, type, data));
        }
        
		if (i > 0 && (width != height || ccNextPOT(width) != width ))
        {
			CCLOG("cocos2d: TexturePVR. WARNING. Mipmap level %u is not squared. Texture won't render correctly. width=%u != height=%u", i, width, height);
        }
        
		err = glGetError();
		if (err != GL_NO_ERROR)
		{
			CCLOG("cocos2d: TexturePVR: Error uploading compressed texture level: %u . glError: 0x%04X", i, err);
			return false;
		}
        
		width = MAX(width >> 1, 1);
		height = MAX(height >> 1, 1);
    }
        
    return true;
}


bool CCTexturePVR::initWithContentsOfFile(const char* path)
{
    int pvrlen = 0;
    
    std::string lowerCase(path);
    for (unsigned int i = 0; i < lowerCase.length(); ++i)
    {
        lowerCase[i] = tolower(lowerCase[i]);
    }
        
    if (lowerCase.find(".ccz") != std::string::npos)
    {
        pvrlen = ZipUtils::ccInflateCCZFile(path, &m_pvrdata);
    }
    else if (lowerCase.find(".gz") != std::string::npos)
    {
        pvrlen = ZipUtils::ccInflateGZipFile(path, &m_pvrdata);
    }
    else
    {
        m_pvrdata = CCFileUtils::sharedFileUtils()->getFileData(path, "rb", (unsigned long *)(&pvrlen));
    }
    
    if (pvrlen < 0)
    {
        this->release();
        return false;
    }
    
    m_uNumberOfMipmaps = 0;

    m_uName = 0;
    m_uWidth = m_uHeight = 0;
    m_bHasAlpha = false;

    m_bRetainName = false; // cocos2d integration

    if (!unpackPVRData(m_pvrdata, pvrlen)  || !createGLTexture())
    {
        deleteData();
        this->release();
        return false;
    }

    deleteData();
    
    return true;
}

bool CCTexturePVR::initWithContentsOfFileAsync(char const * const path)
{
    int pvrlen(0);
    unsigned long rawlen(0);
    
    std::string lowerCase(path);
    for(size_t i(0); i < lowerCase.length(); ++i)
    { lowerCase[i] = tolower(lowerCase[i]); }
        
    if(lowerCase.find(".ccz") != std::string::npos)
    { pvrlen = ZipUtils::ccInflateCCZFile(path, &m_pvrdata); }
    else if(lowerCase.find(".gz") != std::string::npos)
    { pvrlen = ZipUtils::ccInflateGZipFile(path, &m_pvrdata); }
    else
    {
      m_pvrdata = CCFileUtils::sharedFileUtils()->getFileData(
                                    path,
                                    "rb",
                                    &rawlen);
    }
    
    if(pvrlen < 0)
    {
        deleteData();
        release();
        return false;
    }
    else if(pvrlen > 0)
    { rawlen = pvrlen; }
    
    m_uNumberOfMipmaps = 0;
    m_uName = 0;
    m_uWidth = m_uHeight = 0;
    m_bHasAlpha = false;
    m_bRetainName = false;

    /* Defer the creation of the GL name until we're on the
     * main thread. */
    if(!unpackPVRData(m_pvrdata, rawlen))
    {
        deleteData();
        release();
        return false;
    }

    /* Keep our GL texture name alive, even after we're destroyed. */
    setRetainName(true);
    
    return true;
}

CCTexturePVR * CCTexturePVR::pvrTextureWithContentsOfFile(const char* path)
{
    return CCTexturePVR::create(path);
}

CCTexturePVR * CCTexturePVR::create(const char* path)
{
    CCTexturePVR * pTexture = new CCTexturePVR();
    if (pTexture)
    {
        if (pTexture->initWithContentsOfFile(path))
        {
            pTexture->autorelease();
        }
        else
        {
            delete pTexture;
            pTexture = NULL;
        }
    }

    return pTexture;
}

NS_CC_END

