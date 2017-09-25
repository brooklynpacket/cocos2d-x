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

#include "CCTextureCache.h"
#include "CCTexture2D.h"
#include "CCDDS.h"
#include "CCTextureDXT.h"
#include "CCTextureATC.h"
#include "CCTexturePVR.h"
#include "CCTextureASTC.h"
#include "ccMacros.h"
#include "CCDirector.h"
#include "platform/platform.h"
#include "platform/CCFileUtils.h"
#include "platform/CCThread.h"
#include "platform/CCImage.h"
#include "support/ccUtils.h"
#include "CCScheduler.h"
#include "cocoa/CCString.h"
#include <errno.h>
#include <stack>
#include <string>
#include <cctype>
#include <queue>
#include <list>
#include <pthread.h>
#include <vector>

using namespace std;

NS_CC_BEGIN

typedef std::pair<CCObject*, CCTextureCache::AsyncCallback::Func> Functor;

typedef struct _AsyncStruct
{
  std::string filename;
} AsyncStruct;

typedef struct _ImageInfo
{
	AsyncStruct *asyncStruct;
	CCImage		*image;
    CCTextureDXT *dxtTexture;
    CCTextureATC *atcTexture;
    CCTextureASTC *astcTexture;
    class CCTexturePVR *pvrTexture;
	CCImage::EImageFormat imageType;
    bool hasTexture() { return image || dxtTexture || atcTexture || pvrTexture || astcTexture; }
} ImageInfo;

static void (*s_asyncCallback)(CCTextureCache::AsyncCallback const &) = NULL;

static pthread_t s_loadingThread;

static pthread_mutex_t      s_SleepMutex;
static pthread_cond_t       s_SleepCondition;

static pthread_mutex_t      s_callbacksMutex;
static pthread_mutex_t		s_asyncStructQueueMutex;
static pthread_mutex_t      s_imageInfoMutex;

#ifdef EMSCRIPTEN
// Hack to get ASM.JS validation (no undefined symbols allowed).
#define pthread_cond_signal(_)
#endif // EMSCRIPTEN

static pthread_mutex_t      s_pauseMutex;
static pthread_cond_t       s_pauseCondition;
static bool                 s_isPaused{false};

void CCTextureCache::pauseAsync() {
    pthread_mutex_lock(&s_pauseMutex);
    if (!s_isPaused) {
        s_isPaused = true;
    }
    pthread_mutex_unlock(&s_pauseMutex);
}

void CCTextureCache::resumeAsync() {
    pthread_mutex_lock(&s_pauseMutex);
    if (s_isPaused) {
        s_isPaused = false;
        pthread_cond_signal(&s_pauseCondition);
    }
    pthread_mutex_unlock(&s_pauseMutex);
}

static unsigned long s_nAsyncRefCount = 0;

static bool need_quit = false;

static std::queue<AsyncStruct*>* s_pAsyncStructQueue = NULL;
static std::queue<ImageInfo*>*   s_pImageQueue = NULL;

typedef std::map<std::string, std::vector<Functor> > Callbacks_t;
static Callbacks_t s_callbacks;

static CCImage::EImageFormat computeImageFormatType(string& filename)
{
	CCImage::EImageFormat ret = CCImage::kFmtUnKnown;

	if ((std::string::npos != filename.find(".jpg")) || (std::string::npos != filename.find(".jpeg")))
	{
		ret = CCImage::kFmtJpg;
	}
	else if ((std::string::npos != filename.find(".png")) || (std::string::npos != filename.find(".PNG")))
	{
		ret = CCImage::kFmtPng;
	}
    else if ((std::string::npos != filename.find(".tiff")) || (std::string::npos != filename.find(".TIFF")))
    {
        ret = CCImage::kFmtTiff;
    }

	return ret;
}

static void* loadImage(void* data)
{
    // create autorelease pool for iOS
    CCThread thread;
    thread.createAutoreleasePool();

    AsyncStruct *pAsyncStruct = NULL;

    while (true)
    {
        // wait for queue pauses
        pthread_mutex_lock(&s_pauseMutex);
        while (s_isPaused) {
            pthread_cond_wait(&s_pauseCondition, &s_pauseMutex);
        }
        pthread_mutex_unlock(&s_pauseMutex);

        pthread_mutex_lock(&s_asyncStructQueueMutex);// get async struct from queue
        std::queue<AsyncStruct*> *pQueue = s_pAsyncStructQueue;
        if (pQueue->empty())
        {
            pthread_mutex_unlock(&s_asyncStructQueueMutex);
            if (need_quit) {
                break;
            }
            else {
                pthread_cond_wait(&s_SleepCondition, &s_SleepMutex);
                continue;
            }
        }
        else
        {
            pAsyncStruct = pQueue->front();
            pQueue->pop();
            pQueue = NULL;
            pthread_mutex_unlock(&s_asyncStructQueueMutex);
        }		

        /* .compressed is special case. */
        CCTexturePVR *pvr(NULL);
        CCTextureDXT *dxt(NULL);
        CCTextureATC *atc(NULL);
        CCTextureASTC *astc(NULL);
        CCImage *pImage(NULL);
        CCImage::EImageFormat imageType(CCImage::kFmtUnKnown);
        if(pAsyncStruct->filename.find(".pvr") != std::string::npos)
        {
            /* imageType still has to be set for reloadAllTextures(). */
            imageType = CCImage::kFmtPVR;
            /* PVR textures are loaded from disk on this (background) thread
             * and then their GL names will be generated once they get pulled
             * out onto the main thread. */
            pvr = new CCTexturePVR;
            if(!pvr->initWithContentsOfFileAsync(pAsyncStruct->filename.c_str()))
            {
                CCLOG("unable to load PVR %s", pAsyncStruct->filename.c_str());
                // pvr is released if it failed to init, so no delete here
                pvr = nullptr;
            }
        }
        else if(pAsyncStruct->filename.find(".dxt") != std::string::npos)
        {
            imageType = CCImage::kFmtDDS;
            // These come back 'nude' (new), because we are off the main thread and cannot 'autorelease'
            CCDDS* dxtDDS = CCDDS::ddsWithContentsOfFileAsync(pAsyncStruct->filename.c_str());
            dxt = CCTextureDXT::dxtTextureWithDDSAsync(dxtDDS);
            dxtDDS->release();
            if(!dxt)
            {
                CCLOG("unable to load DXT %s", pAsyncStruct->filename.c_str());
                delete dxt;
                dxt = nullptr;
            }
        }
        else if(pAsyncStruct->filename.find(".atc") != std::string::npos)
        {
            imageType = CCImage::kFmtDDS;
            // These come back 'nude' (new), because we are off the main thread and cannot 'autorelease'
            CCDDS* atcDDS = CCDDS::ddsWithContentsOfFileAsync(pAsyncStruct->filename.c_str());
            atc = CCTextureATC::atcTextureWithDDSAsync(atcDDS);
            atcDDS->release();
            if(!atc)
            {
                CCLOG("unable to load ATC %s", pAsyncStruct->filename.c_str());
                delete atc;
                atc = nullptr;
            }
        }
        else if(pAsyncStruct->filename.find(".astc") != std::string::npos)
        {
            /* imageType still has to be set for reloadAllTextures(). */
            imageType = CCImage::kFmtASTC;
            /* PVR textures are loaded from disk on this (background) thread
             * and then their GL names will be generated once they get pulled
             * out onto the main thread. */
            astc = new CCTextureASTC;
            if(!astc->initWithContentsOfFileAsync(pAsyncStruct->filename.c_str()))
            {
                CCLOG("unable to load ASTC %s", pAsyncStruct->filename.c_str());
                delete astc;
                astc = nullptr;
            }
        }
        else
        {
            const char *filename = pAsyncStruct->filename.c_str();

            // compute image type
            imageType = computeImageFormatType(pAsyncStruct->filename);
            if (imageType == CCImage::kFmtUnKnown)
            {
                CCLOG("unsupported format %s",filename);
            } else {
            // generate image			
            pImage = new CCImage();
            if (! pImage->initWithImageFileThreadSafe(filename, imageType))
            {
                    CCLOG("can not load %s", filename);
                delete pImage;
                    pImage=nullptr;
                }
            }
        }

        
        // generate image info
        ImageInfo *pImageInfo = new ImageInfo();
        pImageInfo->asyncStruct = pAsyncStruct;
        pImageInfo->imageType = imageType;
        // if failed to load, all textures below are NULL
        pImageInfo->image = pImage;
        pImageInfo->dxtTexture = dxt;
        pImageInfo->atcTexture = atc;
        pImageInfo->pvrTexture = pvr;
        pImageInfo->astcTexture = astc;

        // put the image info into the queue
        pthread_mutex_lock(&s_imageInfoMutex);
        s_pImageQueue->push(pImageInfo);
        pthread_mutex_unlock(&s_imageInfoMutex);
    }

    if( s_pAsyncStructQueue != NULL )
    {
        delete s_pAsyncStructQueue;
        s_pAsyncStructQueue = NULL;
        delete s_pImageQueue;
        s_pImageQueue = NULL;
        
        pthread_mutex_destroy(&s_asyncStructQueueMutex);
        pthread_mutex_destroy(&s_imageInfoMutex);
        pthread_mutex_destroy(&s_SleepMutex);
        pthread_cond_destroy(&s_SleepCondition);
    }

    return 0;
}

// implementation CCTextureCache

// TextureCache - Alloc, Init & Dealloc
static CCTextureCache *g_sharedTextureCache = NULL;

CCTextureCache::AsyncCallback::AsyncCallback(CCObject * const targ, CCTextureCache::AsyncCallback::Func const sel,
                                             CCTexture2D * const tex, std::string const &file)
  : target(targ), selector(sel), texture(tex), filename(file)
{
  target->retain();
  if(texture) {
    texture->retain();
  }
}

CCTextureCache::AsyncCallback::AsyncCallback(CCTextureCache::AsyncCallback const &ac)
  : target(ac.target), selector(ac.selector), texture(ac.texture), filename(ac.filename)
{
  target->retain();
  if(texture) {
    texture->retain();
  }
}

CCTextureCache::AsyncCallback::~AsyncCallback()
{
  target->release();
  if(texture) {
    texture->release();
  }
}

void CCTextureCache::AsyncCallback::operator ()()
{ (target->*selector)(texture, filename); }

CCTextureCache * CCTextureCache::sharedTextureCache()
{
	if (!g_sharedTextureCache)
    {
		g_sharedTextureCache = new CCTextureCache();
    }
	return g_sharedTextureCache;
}

CCTextureCache::CCTextureCache()
{
	CCAssert(g_sharedTextureCache == NULL, "Attempted to allocate a second instance of a singleton.");

    m_pTextures = new CCDictionary();
}

CCTextureCache::~CCTextureCache()
{
	CCLOGINFO("cocos2d: deallocing CCTextureCache.");
	need_quit = true;
    
    pthread_cond_signal(&s_SleepCondition);
	CC_SAFE_RELEASE(m_pTextures);
}

void CCTextureCache::purgeSharedTextureCache()
{
	CC_SAFE_RELEASE_NULL(g_sharedTextureCache);
}

const char* CCTextureCache::description()
{
    return CCString::createWithFormat("<CCTextureCache | Number of textures = %u>", m_pTextures->count())->getCString();
}

CCDictionary* CCTextureCache::snapshotTextures()
{ 
    CCDictionary* pRet = new CCDictionary();
    CCDictElement* pElement = NULL;
    CCDICT_FOREACH(m_pTextures, pElement)
{
        pRet->setObject(pElement->getObject(), pElement->getStrKey());
    }
    return pRet;
}

void CCTextureCache::addImageAsync(const char *path, CCObject *target,
                                   CCTextureCache::AsyncCallback::Func const selector)
{
	CCAssert(path != NULL, "TextureCache: fileimage MUST not be NULL");	

#ifdef EMSCRIPTEN // NO async textures
    CCTexture2D *texture1 = addImage(path, target);
    std::string const fullpath1 = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(path);
    (target->*selector)(texture1, fullpath1);
    return;
#endif
	CCTexture2D *texture = NULL;

	// optimization

    std::string const fullpath = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(path);
    texture = (CCTexture2D*)m_pTextures->objectForKey(fullpath.c_str());
    bool alreadyFailed = m_failedTextures.find(fullpath) != m_failedTextures.end();

    /* s_callbacks is lazily initialized. */
    if(target)
    {
      /* Has this requester already requested a file? (ignore the previous) */
      removeAsyncImage(target);
    }


	if (texture != NULL || alreadyFailed)
	{
		if (target && selector)
		{
			(target->*selector)(texture, fullpath);
		}
		
		return;
	}

    // lazy init
    if (s_pAsyncStructQueue == NULL)
    {
        s_pAsyncStructQueue = new queue<AsyncStruct*>();
        s_pImageQueue = new queue<ImageInfo*>();
        pthread_mutex_init(&s_asyncStructQueueMutex, NULL);
        pthread_mutex_init(&s_callbacksMutex, NULL);
        pthread_mutex_init(&s_imageInfoMutex, NULL);
        pthread_mutex_init(&s_SleepMutex, NULL);
        pthread_cond_init(&s_SleepCondition, NULL);
        pthread_cond_init(&s_pauseCondition, NULL);
        #ifndef EMSCRIPTEN
		pthread_create(&s_loadingThread, NULL, loadImage, NULL);
        #endif

		need_quit = false;
    }
    
    /* We'll hold onto the target until the callback is complete. */
    if(target)
    {
        target->retain();
    }
    
    /* Check early for multiple requests. */
    pthread_mutex_lock(&s_callbacksMutex);
    
    /* Has someone already requested this file? (attach to the previous) */
    Callbacks_t::iterator const it(s_callbacks.find(fullpath));
    if(it != s_callbacks.end())
    {
      /* We have multiple requests for the same file. */
      Functor const f(target, selector);
      it->second.push_back(f);

      pthread_mutex_unlock(&s_callbacksMutex);
      return;
    }
    pthread_mutex_unlock(&s_callbacksMutex);

    if (0 == s_nAsyncRefCount)
    {
        CCDirector::sharedDirector()->getScheduler()->scheduleSelector(schedule_selector(CCTextureCache::addImageAsyncCallBack), this, 0, false);
    }

    ++s_nAsyncRefCount;

	// generate async struct
	AsyncStruct *data = new AsyncStruct();
    data->filename = fullpath;

	// add async struct into queue
	pthread_mutex_lock(&s_asyncStructQueueMutex);
	s_pAsyncStructQueue->push(data);

    /* Add callback. */
    Functor const functor(target, selector);
    pthread_mutex_lock(&s_callbacksMutex);
    s_callbacks[fullpath].push_back(functor);

    pthread_mutex_unlock(&s_callbacksMutex);
	pthread_mutex_unlock(&s_asyncStructQueueMutex);

    pthread_cond_signal(&s_SleepCondition);
}

void CCTextureCache::setAsyncImageCallback(void (*callback)(AsyncCallback const &))
{ s_asyncCallback = callback; }

void CCTextureCache::removeAsyncImage(CCObject * const target)
{
    pthread_mutex_lock(&s_callbacksMutex);

    for(Callbacks_t::iterator it(s_callbacks.begin()); it != s_callbacks.end(); ++it)
    {
      for(std::vector<Functor>::iterator fit(it->second.begin()); fit != it->second.end(); ++fit)
      {
        /* Search for a selector matching the target. */
        if(fit->first == target) {
          fit->first->release();
          it->second.erase(fit--);
          /* ... continue along, should any other functors
           * reference the target. */
        }
      }
    }

    pthread_mutex_unlock(&s_callbacksMutex);
}

void CCTextureCache::removeAllAsyncImages()
{
    /* Wipe out _all the things_. */
    pthread_mutex_lock(&s_asyncStructQueueMutex);
    pthread_mutex_lock(&s_imageInfoMutex);
    pthread_mutex_lock(&s_callbacksMutex);
    while(!s_pAsyncStructQueue->empty())
    {
        AsyncStruct * const pAsyncStruct{ s_pAsyncStructQueue->front() };
        s_pAsyncStructQueue->pop();
        delete pAsyncStruct;
    }
    while(!s_pImageQueue->empty())
    {
        ImageInfo * const pImageInfo{ s_pImageQueue->front() };
        s_pImageQueue->pop();
        delete pImageInfo;
    }
    while(!s_callbacks.empty())
    {
        Callbacks_t::iterator const it{ s_callbacks.begin() };
        for(auto const &fit : it->second)
        { fit.first->release(); }
        s_callbacks.erase(it);
    }
    pthread_mutex_unlock(&s_callbacksMutex);
    pthread_mutex_unlock(&s_imageInfoMutex);
    pthread_mutex_unlock(&s_asyncStructQueueMutex);
}

void CCTextureCache::addImageAsyncCallBack(float dt)
{
    pthread_mutex_lock(&s_imageInfoMutex);
    // the image is generated in loading thread
    queue<ImageInfo*> *imagesQueue = s_pImageQueue;

    if (imagesQueue->empty())
    {
        pthread_mutex_unlock(&s_imageInfoMutex);
    }
    else
    {
        pthread_mutex_unlock(&s_imageInfoMutex);
        while(true)
        {
            pthread_mutex_lock(&s_imageInfoMutex);
            if(imagesQueue->empty())
            {
                pthread_mutex_unlock(&s_imageInfoMutex);
                return;
            }
            else
            {
                ImageInfo *pImageInfo = imagesQueue->front();
                imagesQueue->pop();
                pthread_mutex_unlock(&s_imageInfoMutex);
                
                AsyncStruct *pAsyncStruct = pImageInfo->asyncStruct;
                const char* filename = pAsyncStruct->filename.c_str();

                if(!pImageInfo->hasTexture()) {
                    executeCallbacks(filename);
                    delete pAsyncStruct;
                    delete pImageInfo;
                    decrementAsyncRefCount();
                    continue;
                }

                CCImage *pImage = pImageInfo->image;
                
                // generate texture in render thread
                CCTexturePVR *pvrTexture(pImageInfo->pvrTexture);
#ifdef ANDROID
                CCTextureDXT *dxtTexture(pImageInfo->dxtTexture);
                CCTextureATC *atcTexture(pImageInfo->atcTexture);
                CCTextureASTC *astcTexture(pImageInfo->astcTexture);
#endif
                CCTexture2D *texture(new CCTexture2D);
                bool success = false;
                
                if(pvrTexture)
                {
                    /* The PVR was created on a separate thread, so it knows no
                     * GL name yet. Try to create that now. */
                    if(pvrTexture->createGLTexture())
                    { success = texture->initWithPVRTexture(pvrTexture); }
                    delete pvrTexture;
                    pvrTexture = NULL;
                }
#ifdef ANDROID
                else if(dxtTexture)
                {
                    if(dxtTexture->createGLTexture())
                    { success = texture->initWithDXTFileAsync(dxtTexture); }
                    delete dxtTexture;
                    dxtTexture = NULL;
                }
                else if(atcTexture)
                {
                    if(atcTexture->createGLTexture())
                    { success = texture->initWithATCFileAsync(atcTexture); }
                    delete atcTexture;
                    atcTexture = NULL;
                }
                else if(astcTexture){
                    if(astcTexture->createGLTexture())
                    { success = texture->initWithASTCFileAsync(astcTexture); }
                    delete astcTexture;
                    astcTexture = NULL;
                }
#endif
                else
                {
#if 0 //TODO: (CC_TARGET_PLATFORM == CC_PLATFORM_IOS)
                    success = texture->initWithImage(pImage, kCCResolutioniPhone);
#else
                    success = texture->initWithImage(pImage);
#endif
                }

                if(!success)
                {
                    texture = NULL;
                    CCLOG("Couldn't add %s", pAsyncStruct->filename.c_str());
                    m_failedTextures.insert(pAsyncStruct->filename);
                }
                else
                {
#if CC_ENABLE_CACHE_TEXTURE_DATA
                    // cache the texture file name
                    VolatileTexture::addImageTexture(texture, filename, pImageInfo->imageType);
#endif
                    
                    // cache the texture
                    m_pTextures->setObject(texture, filename);
                    texture->autorelease();
                }
                
                executeCallbacks(filename, texture);
                decrementAsyncRefCount();
                delete pAsyncStruct;
                delete pImageInfo;
                
                if(pImage)
                { pImage->release(); }

                
            }
        }
    }
}

void CCTextureCache::decrementAsyncRefCount() {
    --s_nAsyncRefCount;
    if (0 == s_nAsyncRefCount)
    {
        CCDirector::sharedDirector()->getScheduler()->unscheduleSelector(schedule_selector(CCTextureCache::addImageAsyncCallBack), this);
    }
}

void CCTextureCache::executeCallbacks(const std::string & filename, cocos2d::CCTexture2D * const texture) {
                pthread_mutex_lock(&s_callbacksMutex);
                /* Get a copy of the functors and clear the original. */
                std::vector<Functor> functors(s_callbacks[filename]);
                s_callbacks.erase(filename);
                pthread_mutex_unlock(&s_callbacksMutex);
                
                for(std::vector<Functor>::iterator it(functors.begin()); it != functors.end(); ++it)
                {
                    /* Copy the functor and remove the original. */
                    Functor const f(*it);
                    functors.erase(it--);
                    
                    CCObject * const target(f.first);
                    CCTextureCache::AsyncCallback::Func const selector(f.second);
                    if (target && selector)
                    {
                        /* Allow the game to specify its own way to handle/throttle callbacks. */
                        if(s_asyncCallback)
                        { s_asyncCallback(CCTextureCache::AsyncCallback(target, selector, texture, filename)); }
                        else
                        { (target->*selector)(texture, filename); }
                        
                        /* It's important that this was removed from the functors
                         * collection first, since the target's dtor could look into
                         * the functor to remove itself, thus causing a double deletion. */
                        target->release();
                    }		
                }
}


CCTexture2D* CCTextureCache::addImage(const char* fileimage, CCObject* target) {
    /* Has this requester already requested a file? (ignore the previous) */
    CCAssert(target, "cocos2d: target is null, cannot remove async image request");
    removeAsyncImage(target);
    return addImage(fileimage);
}

CCTexture2D * CCTextureCache::addImage(const char * path)
{
	CCAssert(path != NULL, "TextureCache: fileimage MUST not be NULL");

    // BPC PATCH
    bool alreadyFailed = m_failedTextures.find(path) != m_failedTextures.end();
    if (alreadyFailed)
    {
        CCLOG("cocos2d: Couldn't add previously-failed image %s to CCTextureCache", path);
        return NULL;
    }
    // END BPC PATCH


	CCTexture2D * texture = NULL;
	// Split up directory and filename
	// MUTEX:
	// Needed since addImageAsync calls this method from a different thread
	
	//pthread_mutex_lock(m_pDictLock);

	// remove possible -HD suffix to prevent caching the same image twice (issue #1040)
    std::string pathKey = path;

    pathKey = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(pathKey.c_str());
    texture = (CCTexture2D*)m_pTextures->objectForKey(pathKey.c_str());

    std::string fullpath = pathKey; // (CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(path));
	if( ! texture ) 
	{
		std::string lowerCase(path);
		for (unsigned int i = 0; i < lowerCase.length(); ++i)
		{
			lowerCase[i] = tolower(lowerCase[i]);
		}
		// all images are handled by UIImage except PVR extension that is handled by our own handler
		do 
		{
			if (std::string::npos != lowerCase.find(".pvr"))
			{
				texture = this->addPVRImage(fullpath.c_str());
			}
            //BPC PATCH
#ifdef ANDROID
            else if(std::string::npos != lowerCase.find(".dds"))
            {
                texture = this->addDDSImage(fullpath.c_str());
            }
            else if(std::string::npos != lowerCase.find(".astc"))
            {
                texture = this->addASTCImage(fullpath.c_str());
            }
#endif
            else
            {
                CCImage::EImageFormat eImageFormat = CCImage::kFmtUnKnown;
                if (std::string::npos != lowerCase.find(".png"))
                {
                    eImageFormat = CCImage::kFmtPng;
                }
			else if (std::string::npos != lowerCase.find(".jpg") || std::string::npos != lowerCase.find(".jpeg"))
			{
                    eImageFormat = CCImage::kFmtJpg;
                }
                else if (std::string::npos != lowerCase.find(".tif") || std::string::npos != lowerCase.find(".tiff"))
                {
                    eImageFormat = CCImage::kFmtTiff;
                }
                
				CCImage image;
                unsigned long nSize = 0;
                unsigned char* pBuffer = CCFileUtils::sharedFileUtils()->getFileData(fullpath.c_str(), "rb", &nSize);
                if (! image.initWithImageData((void*)pBuffer, nSize, eImageFormat))
				{
                    CC_SAFE_DELETE_ARRAY(pBuffer);
                    break;
				}
				else
				{
                    CC_SAFE_DELETE_ARRAY(pBuffer);
			}

				texture = new CCTexture2D();

                if( texture &&
                    texture->initWithImage(&image) )
				{
#if CC_ENABLE_CACHE_TEXTURE_DATA
                    // cache the texture file name
                    VolatileTexture::addImageTexture(texture, fullpath.c_str(), eImageFormat);
#endif

                    m_pTextures->setObject(texture, pathKey.c_str());
                    texture->release();
				}
				else
				{
					CCLOG("cocos2d: Couldn't add image:%s in CCTextureCache", path);
                    /* BPC PATCH */
                    m_failedTextures.insert(path);
                    return NULL; // this was originally falling through, returning an invalid texture
                    /* END BPC PATCH */
				}
			}
		} while (0);
	}

	//pthread_mutex_unlock(m_pDictLock);
	return texture;
}

#ifdef CC_SUPPORT_PVRTC
CCTexture2D* CCTextureCache::addPVRTCImage(const char* path, int bpp, bool hasAlpha, int width)
{
    CCAssert(path != NULL, "TextureCache: fileimage MUST not be nil");
	CCAssert( bpp==2 || bpp==4, "TextureCache: bpp must be either 2 or 4");

	CCTexture2D * texture;

	std::string temp(path);
    
    if ( (texture = (CCTexture2D*)m_pTextures->objectForKey(temp.c_str())) )
	{
		return texture;
	}
	
	// Split up directory and filename
    std::string fullpath( CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(path) );

    unsigned long nLen = 0;
    unsigned char* pData = CCFileUtils::sharedFileUtils()->getFileData(fullpath.c_str(), "rb", &nLen);

	texture = new CCTexture2D();
	
    if( texture->initWithPVRTCData(pData, 0, bpp, hasAlpha, width,
                                   (bpp==2 ? kCCTexture2DPixelFormat_PVRTC2 : kCCTexture2DPixelFormat_PVRTC4)))
	{
        m_pTextures->setObject(texture, temp.c_str());
		texture->autorelease();
	}
	else
	{
		CCLOG("cocos2d: Couldn't add PVRTCImage:%s in CCTextureCache",path);
	}
    CC_SAFE_DELETE_ARRAY(pData);

	return texture;
}
#endif // CC_SUPPORT_PVRTC

CCTexture2D * CCTextureCache::addPVRImage(const char* path)
{
    CCAssert(path != NULL, "TextureCache: fileimage MUST not be nil");

    CCTexture2D* texture = NULL;
    std::string key(path);
    
    if( (texture = (CCTexture2D*)m_pTextures->objectForKey(key.c_str())) ) 
    {
        return texture;
    }

    // Split up directory and filename
    std::string fullpath = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(key.c_str());
    texture = new CCTexture2D();
    if(texture != NULL && texture->initWithPVRFile(fullpath.c_str()) )
    {
#if CC_ENABLE_CACHE_TEXTURE_DATA
        // cache the texture file name
        VolatileTexture::addImageTexture(texture, fullpath.c_str(), CCImage::kFmtPVR);
#endif
        m_pTextures->setObject(texture, key.c_str());
        texture->autorelease();
    }
    else
    {
        CCLOG("cocos2d: Couldn't add PVRImage:%s in CCTextureCache",key.c_str());
        CC_SAFE_DELETE(texture);
    }
    return texture;
}

#ifdef ANDROID
// BPC PATCH START
CCTexture2D * CCTextureCache::addDDSImage(const char* path)
{
	CCAssert(path != NULL, "TextureCache: fileimage MUST not be nill");
	CCTexture2D * tex;
	std::string key(path);
    // remove possible -HD suffix to prevent caching the same image twice (issue #1040)
    CCFileUtils::sharedFileUtils()->ccRemoveHDSuffixFromFile(key);
    
	if( (tex = (CCTexture2D*)m_pTextures->objectForKey(key)) )
	{
		return tex;
	}

    // Split up directory and filename
    std::string fullpath = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(key.c_str());
	tex = new CCTexture2D();
	if( tex->initWithDDSFile(fullpath.c_str()) )
	{
		m_pTextures->setObject(tex, key);
		tex->autorelease();
#if CC_ENABLE_CACHE_TEXTURE_DATA
        // cache the texture file name
        VolatileTexture::addImageTexture(tex, fullpath.c_str(), CCImage::kFmtDDS);
#endif

        return tex;
	}
	else
	{
		CCLOG("cocos2d: Couldn't add DDSImage:%s in CCTextureCache",key.c_str());
        delete tex;
        return NULL;
	}
}

CCTexture2D * CCTextureCache::addASTCImage(const char* path)
{
	CCAssert(path != NULL, "TextureCache: fileimage MUST not be nill");
	CCTexture2D * tex;
	std::string key(path);
    
	if( (tex = (CCTexture2D*)m_pTextures->objectForKey(key)) )
	{
		return tex;
	}
    
    // Split up directory and filename
    std::string fullpath = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(key.c_str());
	tex = new CCTexture2D();
	if( tex->initWithASTCFile(fullpath.c_str()) )
	{
#if CC_ENABLE_CACHE_TEXTURE_DATA
        // cache the texture file name
        VolatileTexture::addImageTexture(tex, fullpath.c_str(), CCImage::kFmtASTC);
#endif
		m_pTextures->setObject(tex, key);
		tex->autorelease();
	}
	else
	{
		CCLOG("cocos2d: Couldn't add ASTCImage:%s in CCTextureCache",key.c_str());
        CC_SAFE_DELETE(tex);
	}
    return tex;
}
// BPC PATCH END
#endif

CCTexture2D* CCTextureCache::addUIImage(CCImage *image, const char *key)
{
    CCAssert(image != NULL, "TextureCache: image MUST not be nil");

	CCTexture2D * texture = NULL;
	// textureForKey() use full path,so the key should be full path
	std::string forKey;
	if (key)
	{
        forKey = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(key);
	}

	// Don't have to lock here, because addImageAsync() will not 
	// invoke opengl function in loading thread.

	do 
	{
		// If key is nil, then create a new texture each time
        if(key && (texture = (CCTexture2D *)m_pTextures->objectForKey(forKey.c_str())))
		{
			break;
		}

		// prevents overloading the autorelease pool
		texture = new CCTexture2D();
        texture->initWithImage(image);

		if(key && texture)
		{
            m_pTextures->setObject(texture, forKey.c_str());
			texture->autorelease();
		}
		else
		{
			CCLOG("cocos2d: Couldn't add UIImage in CCTextureCache");
		}

	} while (0);

#if CC_ENABLE_CACHE_TEXTURE_DATA
    VolatileTexture::addCCImage(texture, image);
#endif
    
	return texture;
}

// TextureCache - Remove

void CCTextureCache::removeAllTextures()
{
	m_pTextures->removeAllObjects();
}

void CCTextureCache::removeUnusedTextures()
{
    /*
    CCDictElement* pElement = NULL;
    CCDICT_FOREACH(m_pTextures, pElement)
    {
        CCLOG("cocos2d: CCTextureCache: texture: %s", pElement->getStrKey());
        CCTexture2D *value = (CCTexture2D*)pElement->getObject();
        if (value->retainCount() == 1)
        {
            CCLOG("cocos2d: CCTextureCache: removing unused texture: %s", pElement->getStrKey());
            m_pTextures->removeObjectForElememt(pElement);
        }
    }
     */
    
    /** Inter engineer zhuoshi sun finds that this way will get better performance
     */    
    if (m_pTextures->count())
    {
        // find elements to be removed
        CCDictElement* pElement = NULL;
        list<CCDictElement*> elementToRemove;
        CCDICT_FOREACH(m_pTextures, pElement)
        {
            CCLOG("cocos2d: CCTextureCache: texture: %s", pElement->getStrKey());
            CCTexture2D *value = (CCTexture2D*)pElement->getObject();
            if (value->retainCount() == 1)
            {
                elementToRemove.push_back(pElement);
            }
            }
        
        // remove elements
        for (list<CCDictElement*>::iterator iter = elementToRemove.begin(); iter != elementToRemove.end(); ++iter)
        {
            CCLOG("cocos2d: CCTextureCache: removing unused texture: %s", (*iter)->getStrKey());
            m_pTextures->removeObjectForElememt(*iter);
        }
    } 
}

void CCTextureCache::removeTexture(CCTexture2D* texture)
{
	if( ! texture )
    {
		return;
    }

    CCArray* keys = m_pTextures->allKeysForObject(texture);
    m_pTextures->removeObjectsForKeys(keys);
}

void CCTextureCache::removeTextureForKey(const char *textureKeyName)
{
	if (textureKeyName == NULL)
	{
		return;
	}

    string fullPath = CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(textureKeyName);
    m_pTextures->removeObjectForKey(fullPath.c_str());
}

CCTexture2D* CCTextureCache::textureForKey(const char* key)
{
    return (CCTexture2D*)m_pTextures->objectForKey(CCFileUtils::sharedFileUtils()->fullPathFromRelativePath(key));
}

void CCTextureCache::reloadAllTextures()
{
#if CC_ENABLE_CACHE_TEXTURE_DATA
    VolatileTexture::reloadAllTextures();
#endif
}

void CCTextureCache::dumpCachedTextureInfo()
{
	unsigned int count = 0;
	unsigned int totalBytes = 0;

    CCDictElement* pElement = NULL;
    CCDICT_FOREACH(m_pTextures, pElement)
	{
        CCTexture2D* tex = (CCTexture2D*)pElement->getObject();
		unsigned int bpp = tex->bitsPerPixelForFormat();
        // Each texture takes up width * height * bytesPerPixel bytes.
		unsigned int bytes = tex->getPixelsWide() * tex->getPixelsHigh() * bpp / 8;
		totalBytes += bytes;
		count++;
		CCLOG("cocos2d: \"%s\" rc=%lu id=%lu %lu x %lu @ %ld bpp => %lu KB",
               pElement->getStrKey(),
			   (long)tex->retainCount(),
			   (long)tex->getName(),
			   (long)tex->getPixelsWide(),
			   (long)tex->getPixelsHigh(),
			   (long)bpp,
			   (long)bytes / 1024);
	}

	CCLOG("cocos2d: CCTextureCache dumpDebugInfo: %ld textures, for %lu KB (%.2f MB)", (long)count, (long)totalBytes / 1024, totalBytes / (1024.0f*1024.0f));
}

#if CC_ENABLE_CACHE_TEXTURE_DATA

std::list<VolatileTexture*> VolatileTexture::textures;
bool VolatileTexture::isReloading = false;

VolatileTexture::VolatileTexture(CCTexture2D *t)
: texture(t)
, m_eCashedImageType(kInvalid)
, m_pTextureData(NULL)
, m_PixelFormat(kTexture2DPixelFormat_RGBA8888)
, m_strFileName("")
, m_FmtImage(CCImage::kFmtPng)
, m_alignment(kCCTextAlignmentCenter)
, m_vAlignment(kCCVerticalTextAlignmentCenter)
, m_strFontName("")
, m_strText("")
, uiImage(NULL)
, m_fFontSize(0.0f)
{
    m_size = CCSizeMake(0, 0);
    m_texParams.minFilter = GL_LINEAR;
    m_texParams.magFilter = GL_LINEAR;
    m_texParams.wrapS = GL_CLAMP_TO_EDGE;
    m_texParams.wrapT = GL_CLAMP_TO_EDGE;
    textures.push_back(this);
}

VolatileTexture::~VolatileTexture()
{
    textures.remove(this);
    CC_SAFE_RELEASE(uiImage);
}

void VolatileTexture::addImageTexture(CCTexture2D *tt, const char* imageFileName, CCImage::EImageFormat format)
{
    if (isReloading)
    {
        return;
        }

    VolatileTexture *vt = findVolotileTexture(tt);

    vt->m_eCashedImageType = kImageFile;
    vt->m_strFileName = imageFileName;
    vt->m_FmtImage    = format;
    vt->m_PixelFormat = tt->getPixelFormat();
}

void VolatileTexture::addCCImage(CCTexture2D *tt, CCImage *image)
{
    VolatileTexture *vt = findVolotileTexture(tt);
    image->retain();
    vt->uiImage = image;
    vt->m_eCashedImageType = kImage;
}

VolatileTexture* VolatileTexture::findVolotileTexture(CCTexture2D *tt)
{
	VolatileTexture *vt = 0;
	std::list<VolatileTexture *>::iterator i = textures.begin();
	while( i != textures.end() )
	{
		VolatileTexture *v = *i++;
        if (v->texture == tt) 
        {
			vt = v;
			break;
		}
	}

	if (!vt)
    {
		vt = new VolatileTexture(tt);
    }

    return vt;
}

void VolatileTexture::addDataTexture(CCTexture2D *tt, void* data, CCTexture2DPixelFormat pixelFormat, const CCSize& contentSize)
{
    if (isReloading)
    {
        return;
    }

    VolatileTexture *vt = findVolotileTexture(tt);

	vt->m_eCashedImageType = kImageData;
	vt->m_pTextureData = data;
	vt->m_PixelFormat = pixelFormat;
	vt->m_TextureSize = contentSize;
}

void VolatileTexture::addStringTexture(CCTexture2D *tt, const char* text, const CCSize& dimensions, CCTextAlignment alignment, 
                                       CCVerticalTextAlignment vAlignment, const char *fontName, float fontSize)
{
    if (isReloading)
    {
        return;
        }

    VolatileTexture *vt = findVolotileTexture(tt);

    vt->m_eCashedImageType = kString;
    vt->m_size        = dimensions;
    vt->m_strFontName = fontName;
    vt->m_alignment   = alignment;
    vt->m_vAlignment  = vAlignment;
    vt->m_fFontSize   = fontSize;
    vt->m_strText     = text;
}

void VolatileTexture::setTexParameters(CCTexture2D *t, ccTexParams *texParams) 
{
    VolatileTexture *vt = findVolotileTexture(t);

    if (texParams->minFilter != GL_NONE)
        vt->m_texParams.minFilter = texParams->minFilter;
    if (texParams->magFilter != GL_NONE)
        vt->m_texParams.magFilter = texParams->magFilter;
    if (texParams->wrapS != GL_NONE)
        vt->m_texParams.wrapS = texParams->wrapS;
    if (texParams->wrapT != GL_NONE)
        vt->m_texParams.wrapT = texParams->wrapT;
}

void VolatileTexture::removeTexture(CCTexture2D *t) 
{

    std::list<VolatileTexture *>::iterator i = textures.begin();
    while( i != textures.end() )
    {
        VolatileTexture *vt = *i++;
        if (vt->texture == t) 
        {
            delete vt;
            break;
        }
    }
}

void VolatileTexture::reloadAllTextures()
{
    isReloading = true;

    CCLOG("reload all texture");
    std::list<VolatileTexture *>::iterator iter = textures.begin();

    while (iter != textures.end())
    {
        VolatileTexture *vt = *iter++;

		switch (vt->m_eCashedImageType)
		{
        case kImageFile:
            {
                CCImage image;
                // BPC PATCH: changed filename comparison to format (PVR, DDS) check

                if (vt->m_FmtImage == CCImage::kFmtPVR)
                {
                    CCTexture2DPixelFormat oldPixelFormat = CCTexture2D::defaultAlphaPixelFormat();
                    CCTexture2D::setDefaultAlphaPixelFormat(vt->m_PixelFormat);

                    vt->texture->initWithPVRFile(vt->m_strFileName.c_str());
                    CCTexture2D::setDefaultAlphaPixelFormat(oldPixelFormat);
                }
                else if (vt->m_FmtImage == CCImage::kFmtDDS)
                {
                    CCTexture2DPixelFormat oldPixelFormat = CCTexture2D::defaultAlphaPixelFormat();
                    CCTexture2D::setDefaultAlphaPixelFormat(vt->m_PixelFormat);

                    vt->texture->initWithDDSFile(vt->m_strFileName.c_str());
                    CCTexture2D::setDefaultAlphaPixelFormat(oldPixelFormat);
                } 
                else 
                {
                    unsigned long nSize = 0;
                    unsigned char* pBuffer = CCFileUtils::sharedFileUtils()->getFileData(vt->m_strFileName.c_str(), "rb", &nSize);

                    if (image.initWithImageData((void*)pBuffer, nSize, vt->m_FmtImage))
                    {
                        CCTexture2DPixelFormat oldPixelFormat = CCTexture2D::defaultAlphaPixelFormat();
                        CCTexture2D::setDefaultAlphaPixelFormat(vt->m_PixelFormat);
                        vt->texture->initWithImage(&image);
                        CCTexture2D::setDefaultAlphaPixelFormat(oldPixelFormat);
                    }

                    CC_SAFE_DELETE_ARRAY(pBuffer);
                }
            }
            break;
		case kImageData:
			{
                vt->texture->initWithData(vt->m_pTextureData, 
                                          vt->m_PixelFormat, 
                                          vt->m_TextureSize.width, 
                                          vt->m_TextureSize.height, 
                                          vt->m_TextureSize);
			}
			break;
		case kString:
			{
				vt->texture->initWithString(vt->m_strText.c_str(),
					vt->m_size,
					vt->m_alignment,
                    vt->m_vAlignment,
					vt->m_strFontName.c_str(),
					vt->m_fFontSize);
			}
			break;
        case kImage:
            {
                vt->texture->initWithImage(vt->uiImage);
            }
            break;
		default:
			break;
		}
        vt->texture->setTexParameters(&vt->m_texParams);
    }

    isReloading = false;
}

#endif // CC_ENABLE_CACHE_TEXTURE_DATA

NS_CC_END
