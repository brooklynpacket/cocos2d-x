/****************************************************************************
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

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

#include "renderer/CCTextureCache.h"

#include <errno.h>
#include <stack>
#include <cctype>
#include <list>

#include "renderer/CCTexture2D.h"
#include "base/ccMacros.h"
#include "base/ccUTF8.h"
#include "base/CCDirector.h"
#include "base/CCScheduler.h"
#include "platform/CCFileUtils.h"
#include "base/ccUtils.h"
#include "base/CCNinePatchImageParser.h"
#include "renderer/backend/Device.h"
//#include "renderer/backend/StringUtils.h"


using namespace std;

NS_CC_BEGIN

std::string TextureCache::s_etc1AlphaFileSuffix = "@alpha";

// implementation TextureCache

void TextureCache::setETC1AlphaFileSuffix(const std::string& suffix)
{
    s_etc1AlphaFileSuffix = suffix;
}

std::string TextureCache::getETC1AlphaFileSuffix()
{
    return s_etc1AlphaFileSuffix;
}

//BPC PATCH
static pthread_mutex_t      s_pauseMutex;
static pthread_cond_t       s_pauseCondition;
static bool                 s_isPaused{false};

void TextureCache::pauseAsync() {
    pthread_mutex_lock(&s_pauseMutex);
    if (!s_isPaused) {
        s_isPaused = true;
    }
    pthread_mutex_unlock(&s_pauseMutex);
}

void TextureCache::resumeAsync() {
    pthread_mutex_lock(&s_pauseMutex);
    if (s_isPaused) {
        s_isPaused = false;
        pthread_cond_signal(&s_pauseCondition);
    }
    pthread_mutex_unlock(&s_pauseMutex);
}
//END BPC PATCH

TextureCache::TextureCache()
: _loadingThread(nullptr)
, _needQuit(false)
, _asyncRefCount(0)
{
    pthread_mutex_init(&s_pauseMutex, NULL);
    pthread_cond_init(&s_pauseCondition, NULL);
}

TextureCache::~TextureCache()
{
    CCLOGINFO("deallocing TextureCache: %p", this);

	// BPC PATCH
	std::lock_guard<std::mutex> locky(_texturesMutex); 
	// END BPC PATCH   
	for (auto& texture : _textures)
        texture.second->release();

    CC_SAFE_DELETE(_loadingThread);
}

std::string TextureCache::getDescription() const
{
    return StringUtils::format("<TextureCache | Number of textures = %d>", static_cast<int>(_textures.size()));
}

Texture2D* TextureCache::findTexture(std::string const& fullpath) const {
    Texture2D* tex = nullptr;
    std::lock_guard<std::mutex> locky(_texturesMutex);
    auto it = _textures.find(fullpath);
    if( it != _textures.end() )
        tex = it->second;
    
    return tex;}

/**
 The addImageAsync logic follow the steps:
 - find the image has been add or not, if not add an AsyncStruct to _requestQueue  (GL thread)
 - get AsyncStruct from _requestQueue, load res and fill image data to AsyncStruct.image, then add AsyncStruct to _responseQueue (Load thread)
 - on schedule callback, get AsyncStruct from _responseQueue, convert image to texture, then delete AsyncStruct (GL thread)

 the Critical Area include these members:
 - _requestQueue: locked by _requestMutex
 - _responseQueue: locked by _responseMutex

 the object's life time:
 - AsyncStruct: construct and destruct in GL thread
 - image data: new in Load thread, delete in GL thread(by Image instance)

 Note:
 - all AsyncStruct referenced in _asyncStructQueue, for unbind function use.

 How to deal add image many times?
 - At first, this situation is abnormal, we only ensure the logic is correct.
 - If the image has been loaded, the after load image call will return immediately.
 - If the image request is in queue already, there will be more than one request in queue,
 - In addImageAsyncCallback, will deduplicate the request to ensure only create one texture.

 Does process all response in addImageAsyncCallback consume more time?
 - Convert image to texture faster than load image from disk, so this isn't a
 problem.

 Call unbindImageAsync(path) to prevent the call to the callback when the
 texture is loaded.
 */
void TextureCache::addImageAsync(const std::string &path, const std::function<void(Texture2D*)>& callback, Ref * const target)
{
    addImageAsync( path, callback, path, target);
}

/**
 The addImageAsync logic follow the steps:
 - find the image has been add or not, if not add an AsyncStruct to _requestQueue  (GL thread)
 - get AsyncStruct from _requestQueue, load res and fill image data to AsyncStruct.image, then add AsyncStruct to _responseQueue (Load thread)
 - on schedule callback, get AsyncStruct from _responseQueue, convert image to texture, then delete AsyncStruct (GL thread)
 
 the Critical Area include these members:
 - _requestQueue: locked by _requestMutex
 - _responseQueue: locked by _responseMutex
 
 the object's life time:
 - AsyncStruct: construct and destruct in GL thread
 - image data: new in Load thread, delete in GL thread(by Image instance)
 
 Note:
 - all AsyncStruct referenced in _asyncStructQueue, for unbind function use.
 
 How to deal add image many times?
 - At first, this situation is abnormal, we only ensure the logic is correct.
 - If the image has been loaded, the after load image call will return immediately.
 - If the image request is in queue already, there will be more than one request in queue,
 - In addImageAsyncCallback, will deduplicate the request to ensure only create one texture.
 
 Does process all response in addImageAsyncCallback consume more time?
 - Convert image to texture faster than load image from disk, so this isn't a
 problem.

 The callbackKey allows to unbind the callback in cases where the loading of
 path is requested by several sources simultaneously. Each source can then
 unbind the callback independently as needed whilst a call to
 unbindImageAsync(path) would be ambiguous.
 */
void TextureCache::addImageAsync(const std::string &path, const std::function<void(Texture2D*)>& callback, const std::string& callbackKey, Ref * const target)
{
    Texture2D *texture = nullptr;

    std::string fullpath = FileUtils::getInstance()->fullPathForFilename(path);

    texture = findTexture(fullpath);

    if (texture != nullptr)
    {
        if (callback) callback(texture);
        return;
    }

    // check if file exists
    if (fullpath.empty() || !FileUtils::getInstance()->isFileExist(fullpath)) {
        if (callback) callback(nullptr);
        return;
    }

    // lazy init
    if (_asyncStructQueue == nullptr)
    {             
        _asyncStructQueue = new deque<AsyncStruct*>();
        _imageInfoQueue   = new deque<ImageInfo*>();        

        // create a new thread to load images
        _needQuit = false;
        _loadingThread = new (std::nothrow) std::thread(&TextureCache::loadImage, this);
    }

    if (0 == _asyncRefCount)
    {
        Director::getInstance()->getScheduler()->schedule(CC_SCHEDULE_SELECTOR(TextureCache::addImageAsyncCallBack), this, 0, false);
    }

    ++_asyncRefCount;
    
    bool found = false;
	{
    	std::unique_lock<std::mutex> imageInfoLock(_imageInfoMutex);
    	auto infoIt(_imageInfoQueue->begin());
    	while (infoIt != _imageInfoQueue->end()) {
        	if((*infoIt)->asyncStruct->callbackKey == callbackKey) {
            	(*infoIt)->asyncStruct->addRequestor(target, callback);
            	found = true;
            	break;
        	}
        	else {
            	++infoIt;
        	}
    	}
	}
    
    if(found == false) {
        std::unique_lock<std::mutex> ul(_asyncStructQueueMutex);
        auto structIt(_asyncStructQueue->begin());
        while(structIt != _asyncStructQueue->end()){
            if((*structIt)->callbackKey == callbackKey){
                (*structIt)->addRequestor(target, callback);
                found = true;
                break;
            }else{
                ++structIt;
            }
        }
        // generate async struct
        if(found == false){
            AsyncStruct *data = new (std::nothrow) AsyncStruct(fullpath, callback, callbackKey, target);
            // add async struct into queue
            _asyncStructQueue->push_back(data);
        }
    }
    
    _sleepCondition.notify_one();
}

void TextureCache::unbindImageAsync(const std::string& callbackKey)
{
    _imageInfoMutex.lock();
    if (_imageInfoQueue && !_imageInfoQueue->empty())
    {
        auto found = std::find_if(_imageInfoQueue->begin(), _imageInfoQueue->end(), [&callbackKey](ImageInfo* ptr)->bool{ return ptr->asyncStruct->callbackKey == callbackKey; });
        if (found != _imageInfoQueue->end())
        {
            for(auto pair : (*found)->asyncStruct->requestorToCallbacks){
                pair.first->release();
            }
            (*found)->asyncStruct->requestorToCallbacks.clear();
        }
    }
    _imageInfoMutex.unlock();
}

void TextureCache::unbindAllImageAsync()
{
    _imageInfoMutex.lock();
    if (_imageInfoQueue && !_imageInfoQueue->empty())
    {
        std::for_each(_imageInfoQueue->begin(), _imageInfoQueue->end(),
              [](ImageInfo* ptr) {
                    for(auto pair : ptr->asyncStruct->requestorToCallbacks){
                        pair.first->release();
                    }
                  ptr->asyncStruct->requestorToCallbacks.clear();
              });
    }
    _imageInfoMutex.unlock();
}

void TextureCache::loadImage()
{
    AsyncStruct *asyncStruct = nullptr;
    while (true)
    {
        // wait for queue pauses
        pthread_mutex_lock(&s_pauseMutex);
        while (s_isPaused) {
            pthread_cond_wait(&s_pauseCondition, &s_pauseMutex);
        }
        pthread_mutex_unlock(&s_pauseMutex);
        
        std::deque<AsyncStruct*> *pQueue = _asyncStructQueue;
        _asyncStructQueueMutex.lock();
        if (pQueue->empty())
        {
            _asyncStructQueueMutex.unlock();
            if (_needQuit) {
                break;
            }
            else {
                std::unique_lock<std::mutex> lk(_sleepMutex);
                _sleepCondition.wait(lk);
                continue;
            }
        }
        else
        {
            asyncStruct = pQueue->front();
            pQueue->pop_front();
            _asyncStructQueueMutex.unlock();
        }        

        Image *image = nullptr;
        Image *imageAlpha = nullptr;
        bool generateImage = false;
        bool passedToOtherTask = false;
        
        Texture2D* foundtex = findTexture(asyncStruct->filename);
        if( foundtex == nullptr )
        {
           _imageInfoMutex.lock();
           ImageInfo *imageInfo;
           size_t pos = 0;
           size_t infoSize = _imageInfoQueue->size();
           for (; pos < infoSize; pos++)
           {
               imageInfo = (*_imageInfoQueue)[pos];
               if(imageInfo->asyncStruct->filename.compare(asyncStruct->filename) == 0){
                   // found a dupe already processed; give our callback to them
                   for(const auto& reqPair : asyncStruct->requestorToCallbacks){
                       imageInfo->asyncStruct->addRequestor(reqPair.first, reqPair.second);
                   }
                   passedToOtherTask = true;
                   break;
               }
           }
           _imageInfoMutex.unlock();
           generateImage = !passedToOtherTask;
        }
        //else texture was in cache - keep it alive until callback completes

        if (generateImage)
        {
            const std::string& filename = asyncStruct->filename;
            // generate image
            image = new (std::nothrow) Image();
            if (image && !image->initWithImageFileThreadSafe(filename))
            {
                CC_SAFE_RELEASE(image);
                CCLOG("can not load %s", filename.c_str());
                continue;
            }

			// ETC1 ALPHA supports.
        	if (image->getFileType() == Image::Format::ETC && !s_etc1AlphaFileSuffix.empty())
	        { // check whether alpha texture exists & load it
    	        auto alphaFile = filename + s_etc1AlphaFileSuffix;
                if (FileUtils::getInstance()->isFileExist(alphaFile)) {
                    imageAlpha = new (std::nothrow) Image();
            	    imageAlpha->initWithImageFileThreadSafe(alphaFile);
                }
        	}
        }    

        if (passedToOtherTask){
            delete asyncStruct;
            continue;
        }
        
        // BPC PATCH
        // generate image info
        ImageInfo *imageInfo = new (std::nothrow) ImageInfo();
        imageInfo->asyncStruct = asyncStruct;
        imageInfo->image = image;
        imageInfo->imageAlpha = imageAlpha;
        // need to keep texture alive if already in cache
        imageInfo->setTexture(foundtex);
        // END BPC PATCH

        // put the image info into the queue
        _imageInfoMutex.lock();
        _imageInfoQueue->push_back(imageInfo);
        _imageInfoMutex.unlock();
    }
    
	if(_asyncStructQueue != nullptr)
    {
        delete _asyncStructQueue;
	    _asyncStructQueue = nullptr;
        delete _imageInfoQueue;
	    _imageInfoQueue = nullptr;
    }
}

void TextureCache::addImageAsyncCallBack(float dt)
{
    // the image is generated in loading thread
    std::deque<ImageInfo*> *imagesQueue = _imageInfoQueue;

    _imageInfoMutex.lock();
    if (imagesQueue->empty())
    {
        _imageInfoMutex.unlock();
    }
    else
    {
        ImageInfo *imageInfo = imagesQueue->front();
        imagesQueue->pop_front();
        _imageInfoMutex.unlock();

        AsyncStruct *asyncStruct = imageInfo->asyncStruct;
        Image* image = imageInfo->image;

        const std::string& filename = asyncStruct->filename;

        Texture2D *texture = nullptr;
        if (image)
        {
			// generate texture in render thread
           	texture = new (std::nothrow) Texture2D();

            texture->initWithImage(image);
            //parse 9-patch info
            this->parseNinePatchImage(image, texture, filename);
#if CC_ENABLE_CACHE_TEXTURE_DATA
            // cache the texture file name
            VolatileTextureMgr::addImageTexture(texture, filename);
#endif
        	// cache the texture. retain it, since it is added in the map
            {
               	std::lock_guard<std::mutex> locky(_texturesMutex);
               	_textures.emplace(filename, texture);
           	}
           	texture->retain();

            texture->autorelease();
            
            // ETC1 ALPHA supports.
            Image* imageAlpha = imageInfo->imageAlpha;
            if (imageAlpha && imageAlpha->getFileType() == Image::Format::ETC) {
                auto alphaTexture = new(std::nothrow) Texture2D();
                if(alphaTexture != nullptr && alphaTexture->initWithImage(imageAlpha)) {
                    texture->setAlphaTexture(alphaTexture);
                }
                CC_SAFE_RELEASE(alphaTexture);
            }
        }
        else
        {
            texture = imageInfo->foundTex;
            CCASSERT(texture!=nullptr, "No image, no texture for callback.");
        }
        
        for(auto pair : asyncStruct->requestorToCallbacks){
            if(pair.second){
                pair.second(texture);
            }
        }
        
        if(image)
        {
            image->release();
        }
        
        delete asyncStruct;
        delete imageInfo;

        --_asyncRefCount;
        if (0 == _asyncRefCount)
        {
            Director::getInstance()->getScheduler()->unschedule(CC_SCHEDULE_SELECTOR(TextureCache::addImageAsyncCallBack), this);
        }
    }
    
    /* BPC_PATCH */
    if(dt < 1 && _asyncRefCount>0){
        // Kludge! repeat up to 5 times on fast devices
        // instead of one per frame
        addImageAsyncCallBack(dt + 1/5.0);
    }
    /* BPC_PATCH */
}

Texture2D * TextureCache::addImage(const std::string &path)
{
    Texture2D * texture = nullptr;
    Image* image = nullptr;
    // Split up directory and filename
    // MUTEX:
    // Needed since addImageAsync calls this method from a different thread

    std::string fullpath = FileUtils::getInstance()->fullPathForFilename(path);
    if (fullpath.size() == 0)
    {
        return nullptr;
    }
    
    texture = findTexture(fullpath);
    
    if (! texture)
    {
        // all images are handled by UIImage except PVR extension that is handled by our own handler
        do
        {
            image = new (std::nothrow) Image();
            CC_BREAK_IF(nullptr == image);

            bool bRet = image->initWithImageFile(fullpath);
            CC_BREAK_IF(!bRet);
            
            // BPC PATCH - 1x1 PVRs causin' gpu errors
            if(image->isCompressed() && image->getWidth() * image->getHeight() == 1) {
                break;
            }

            texture = new (std::nothrow) Texture2D();

            if (texture && texture->initWithImage(image))
            {
#if CC_ENABLE_CACHE_TEXTURE_DATA
                // cache the texture file name
                VolatileTextureMgr::addImageTexture(texture, fullpath);
#endif
                // texture already retained, no need to re-retain it
                std::lock_guard<std::mutex> locky(_texturesMutex);
                _textures.emplace(fullpath, texture);

                //-- ANDROID ETC1 ALPHA SUPPORTS.
                std::string alphaFullPath = path + s_etc1AlphaFileSuffix;
                if (image->getFileType() == Image::Format::ETC && !s_etc1AlphaFileSuffix.empty() && FileUtils::getInstance()->isFileExist(alphaFullPath))
                {
                    Image alphaImage;
                    if (alphaImage.initWithImageFile(alphaFullPath))
                    {
                        Texture2D *pAlphaTexture = new(std::nothrow) Texture2D;
                        if(pAlphaTexture != nullptr && pAlphaTexture->initWithImage(&alphaImage)) {
                            texture->setAlphaTexture(pAlphaTexture);
                        }
                        CC_SAFE_RELEASE(pAlphaTexture);
                    }
                }

                //parse 9-patch info
                this->parseNinePatchImage(image, texture, path);
            }
            else
            {
                CCLOG("cocos2d: Couldn't create texture for file:%s in TextureCache", path.c_str());
                CC_SAFE_RELEASE(texture);
                texture = nullptr;
            }
        } while (0);
    }

    CC_SAFE_RELEASE(image);

    return texture;
}

void TextureCache::parseNinePatchImage(cocos2d::Image *image, cocos2d::Texture2D *texture, const std::string& path)
{
    if (NinePatchImageParser::isNinePatchImage(path))
    {
        Rect frameRect = Rect(0, 0, image->getWidth(), image->getHeight());
        NinePatchImageParser parser(image, frameRect, false);
        texture->addSpriteFrameCapInset(nullptr, parser.parseCapInset());
    }

}

Texture2D* TextureCache::addImage(Image *image, const std::string &key)
{
    CCASSERT(image != nullptr, "TextureCache: image MUST not be nil");
    CCASSERT(image->getData() != nullptr, "TextureCache: image MUST not be nil");

    Texture2D * texture = nullptr;

    do
    {
        texture = findTexture(key);
        if(texture) {
            break;
        }

        texture = new (std::nothrow) Texture2D();

        if (texture)
        {
            if (texture->initWithImage(image))
            {
				//BPC PATCH
				std::lock_guard<std::mutex> locky(_texturesMutex);
				//BPC PATCH
                _textures.emplace(key, texture);
            }
            else
            {
                CC_SAFE_RELEASE(texture);
                texture = nullptr;
                CCLOG("cocos2d: initWithImage failed!");
            }
        }
        else
        {
            CCLOG("cocos2d: Allocating memory for Texture2D failed!");
        }

    } while (0);

#if CC_ENABLE_CACHE_TEXTURE_DATA
    VolatileTextureMgr::addImage(texture, image);
#endif

    return texture;
}

bool TextureCache::reloadTexture(const std::string& fileName)
{
    Texture2D * texture = nullptr;
    Image * image = nullptr;

    std::string fullpath = FileUtils::getInstance()->fullPathForFilename(fileName);
    if (fullpath.size() == 0)
    {
        return false;
    }

    texture = findTexture(fullpath);

    bool ret = false;
    if (!texture) {
        texture = this->addImage(fullpath);
        ret = (texture != nullptr);
    }
    else
    {
        do {
            image = new (std::nothrow) Image();
            CC_BREAK_IF(nullptr == image);

            bool bRet = image->initWithImageFile(fullpath);
            CC_BREAK_IF(!bRet);

            ret = texture->initWithImage(image);
        } while (0);
    }

    CC_SAFE_RELEASE(image);

    return ret;
}

// TextureCache - Remove

void TextureCache::removeAllTextures()
{
    // BPC PATCH
	std::lock_guard<std::mutex> locky(_texturesMutex);
	// BPC PATCH
	for (auto& texture : _textures) {
        texture.second->release();
    }
    _textures.clear();
}

void TextureCache::removeUnusedTextures()
{
    std::lock_guard<std::mutex> locky(_texturesMutex);
    for( auto it=_textures.cbegin(); it!=_textures.cend(); /* nothing */) {
        Texture2D *tex = it->second;
        if (tex->getReferenceCount() == 1) {
            CCLOG("cocos2d: TextureCache: removing unused texture: %s", it->first.c_str());

            tex->release();
            _textures.erase(it++);
        } else {
            ++it;
        }

    }
}

void TextureCache::removeTexture(Texture2D* texture)
{
    if (!texture)
    {
        return;
    }

    std::lock_guard<std::mutex> locky(_texturesMutex);
    for( auto it=_textures.cbegin(); it!=_textures.cend(); /* nothing */ ) {
        if( it->second == texture ) {
            texture->release();
            _textures.erase(it++);
            break;
        }
        else
            ++it;
    }
}

void TextureCache::removeTextureForKey(const std::string &textureKeyName)
{
    std::string key = textureKeyName;
    std::lock_guard<std::mutex> locky(_texturesMutex);
    auto it = _textures.find(key);

    if (it == _textures.end()) {
        key = FileUtils::getInstance()->fullPathForFilename(textureKeyName);
        it = _textures.find(key);
    }

    if (it != _textures.end()) {
        it->second->release();
        _textures.erase(it);
    }
}

void TextureCache::removeAsyncImage(Ref * const target, string const & filename)
{
    auto key = FileUtils::getInstance()->fullPathForFilename(filename);
    if(_asyncStructQueue == nullptr) return;
    _asyncStructQueueMutex.lock();
    
    auto structIt(_asyncStructQueue->begin());
    while (structIt != _asyncStructQueue->end()) {
        if((*structIt)->filename == filename) {
            (*structIt)->removeRequestor(target);
            if((*structIt)->requestorToCallbacks.empty()){
                delete (*structIt);
                structIt = _asyncStructQueue->erase(structIt);
            }
            break;
        }
        else {
            ++structIt;
        }
    }
    _asyncStructQueueMutex.unlock();
    
    _imageInfoMutex.lock();
    auto infoIt(_imageInfoQueue->begin());
    while (infoIt != _imageInfoQueue->end()) {
        if((*infoIt)->asyncStruct->filename == filename) {
                if( (*infoIt)->asyncStruct->requestorToCallbacks.empty()){
                delete (*infoIt)->asyncStruct;
                delete (*infoIt);
                infoIt = _imageInfoQueue->erase(infoIt);
            }
            break;
        }
        else {
            ++infoIt;
        }
    }
    _imageInfoMutex.unlock();
}

Texture2D* TextureCache::getTextureForKey(const std::string &textureKeyName) const
{
    std::string key = textureKeyName;
    std::lock_guard<std::mutex> locky(_texturesMutex);
    auto it = _textures.find(key);

    if (it == _textures.end()) {
        key = FileUtils::getInstance()->fullPathForFilename(textureKeyName);
        it = _textures.find(key);
    }

    if (it != _textures.end())
        return it->second;
    return nullptr;
}

std::string TextureCache::getTextureFilePath(cocos2d::Texture2D* texture) const
{
    for (auto& item : _textures)
    {
        if (item.second == texture)
        {
            return item.first;
            break;
        }
    }
    return "";
}

void TextureCache::waitForQuit()
{
    // notify sub thread to quick
    std::unique_lock<std::mutex> ul(_asyncStructQueueMutex);
    _needQuit = true;
    _sleepCondition.notify_one();
    ul.unlock();
    if (_loadingThread) _loadingThread->join();
}

std::string TextureCache::getCachedTextureInfo() const
{
    std::string buffer;
    char buftmp[4096];

    unsigned int count = 0;
    unsigned int totalBytes = 0;

    // BPC PATCH
	std::lock_guard<std::mutex> locky(_texturesMutex);	
	// BPC PATCH
    for (auto& texture : _textures) {

        memset(buftmp, 0, sizeof(buftmp));


        Texture2D* tex = texture.second;
        unsigned int bpp = tex->getBitsPerPixelForFormat();
        // Each texture takes up width * height * bytesPerPixel bytes.
        auto bytes = tex->getPixelsWide() * tex->getPixelsHigh() * bpp / 8;
        totalBytes += bytes;
        count++;
        
        const long kb = long(bytes) / 1024;
        const bool big = kb >= 512;
        snprintf(buftmp,sizeof(buftmp)-1,"%lu %s \"%s\" rc=%lu id=%p %lu x %lu @ %ld bpp \n",
                kb,
                 big? "!KB!" : "KB:",
               texture.first.c_str(),
               (long)tex->getReferenceCount(),
               tex->getBackendTexture(),
               (long)tex->getPixelsWide(),
               (long)tex->getPixelsHigh(),
               (long)bpp);
        
        CCLOG("%s", buftmp);
    }

    snprintf(buftmp, sizeof(buftmp) - 1, "TextureCache dumpDebugInfo: %ld textures, for %lu KB (%.2f MB)\n", (long)count, (long)totalBytes / 1024, totalBytes / (1024.0f*1024.0f));
    buffer += buftmp;

    return buffer;
}

std::unordered_map<std::string, Texture2D*> TextureCache::getCachedTextures() const{
    std::lock_guard<std::mutex> locky(_texturesMutex);
    std::unordered_map<std::string, Texture2D*> textureMap = _textures;
    return textureMap;
}

void TextureCache::renameTextureWithKey(const std::string& srcName, const std::string& dstName)
{
    std::string key = srcName;
    auto it = _textures.find(key);

    if (it == _textures.end()) {
        key = FileUtils::getInstance()->fullPathForFilename(srcName);
        it = _textures.find(key);
    }

    if (it != _textures.end()) {
        std::string fullpath = FileUtils::getInstance()->fullPathForFilename(dstName);
        Texture2D* tex = it->second;

        Image* image = new (std::nothrow) Image();
        if (image)
        {
            bool ret = image->initWithImageFile(dstName);
            if (ret)
            {
                tex->initWithImage(image);
                _textures.emplace(fullpath, tex);
                _textures.erase(it);
            }
            CC_SAFE_DELETE(image);
        }
    }
}

#if CC_ENABLE_CACHE_TEXTURE_DATA

std::list<VolatileTexture*> VolatileTextureMgr::_textures;
bool VolatileTextureMgr::_isReloading = false;

VolatileTexture::VolatileTexture(Texture2D *t)
: _texture(t)
, _uiImage(nullptr)
, _cashedImageType(kInvalid)
, _textureData(nullptr)
, _pixelFormat(backend::PixelFormat::RGBA8888)
, _fileName("")
, _text("")
{
}

VolatileTexture::~VolatileTexture()
{
    CC_SAFE_RELEASE(_uiImage);
}

void VolatileTextureMgr::addImageTexture(Texture2D *tt, const std::string& imageFileName)
{
    if (_isReloading)
    {
        return;
    }

    VolatileTexture *vt = findVolotileTexture(tt);

    vt->_cashedImageType = VolatileTexture::kImageFile;
    vt->_fileName = imageFileName;
    vt->_pixelFormat = tt->getPixelFormat();
}

void VolatileTextureMgr::addImage(Texture2D *tt, Image *image)
{
    if (tt == nullptr || image == nullptr)
        return;
    
    VolatileTexture *vt = findVolotileTexture(tt);
    image->retain();
    vt->_uiImage = image;
    vt->_cashedImageType = VolatileTexture::kImage;
    vt->_pixelFormat = tt->getPixelFormat();
}

VolatileTexture* VolatileTextureMgr::findVolotileTexture(Texture2D *tt)
{
    VolatileTexture *vt = nullptr;
    for (const auto& texture : _textures)
    {
        VolatileTexture *v = texture;
        if (v->_texture == tt)
        {
            vt = v;
            break;
        }
    }

    if (!vt)
    {
        vt = new (std::nothrow) VolatileTexture(tt);
        _textures.push_back(vt);
    }

    return vt;
}

void VolatileTextureMgr::addDataTexture(Texture2D *tt, void* data, int dataLen, backend::PixelFormat pixelFormat, const Size& contentSize)
{
    if (_isReloading)
    {
        return;
    }

    VolatileTexture *vt = findVolotileTexture(tt);

    vt->_cashedImageType = VolatileTexture::kImageData;
    vt->_textureData = data;
    vt->_dataLen = dataLen;
    vt->_pixelFormat = pixelFormat;
    vt->_textureSize = contentSize;
}

void VolatileTextureMgr::addStringTexture(Texture2D *tt, const char* text, const FontDefinition& fontDefinition)
{
    if (_isReloading)
    {
        return;
    }

    VolatileTexture *vt = findVolotileTexture(tt);

    vt->_cashedImageType = VolatileTexture::kString;
    vt->_text = text;
    vt->_fontDefinition = fontDefinition;
}

void VolatileTextureMgr::removeTexture(Texture2D *t)
{
    for (auto& item : _textures)
    {
        VolatileTexture *vt = item;
        if (vt->_texture == t)
        {
            _textures.remove(vt);
            delete vt;
            break;
        }
    }
}

void VolatileTextureMgr::reloadAllTextures()
{
    _isReloading = true;
    CCLOG("reload all texture");

    for (auto& texture : _textures)
    {
        VolatileTexture *vt = texture;

        switch (vt->_cashedImageType)
        {
        case VolatileTexture::kImageFile:
        {
            reloadTexture(vt->_texture, vt->_fileName, vt->_pixelFormat);

            // etc1 support check whether alpha texture exists & load it
            auto alphaFile = vt->_fileName + TextureCache::getETC1AlphaFileSuffix();
            reloadTexture(vt->_texture->getAlphaTexture(), alphaFile, vt->_pixelFormat);
        }
        break;
        case VolatileTexture::kImageData:
        {
            vt->_texture->initWithData(vt->_textureData,
                vt->_dataLen,
                vt->_pixelFormat,
                vt->_textureSize.width,
                vt->_textureSize.height,
                vt->_textureSize);
        }
        break;
        case VolatileTexture::kString:
        {
            vt->_texture->initWithString(vt->_text.c_str(), vt->_fontDefinition);
        }
        break;
        case VolatileTexture::kImage:
        {
            vt->_texture->initWithImage(vt->_uiImage, vt->_pixelFormat);
        }
        break;
        default:
            break;
        }
    }

    _isReloading = false;
}

void VolatileTextureMgr::reloadTexture(Texture2D* texture, const std::string& filename, backend::PixelFormat pixelFormat)
{
    if (!texture)
        return;

    Image* image = new (std::nothrow) Image();
    Data data = FileUtils::getInstance()->getDataFromFile(filename);

    if (image && image->initWithImageData(data.getBytes(), data.getSize()))
        texture->initWithImage(image, pixelFormat);

    CC_SAFE_RELEASE(image);
}

#endif // CC_ENABLE_CACHE_TEXTURE_DATA

NS_CC_END

