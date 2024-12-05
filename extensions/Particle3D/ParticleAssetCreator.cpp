//
//  ParticleAssetCreator.cpp
//  cocos2d_libs
//
//  Created by Harry 😎 on 11/18/16.
//
//

#include "ParticleAssetCreator.h"
#include "base/CCDirector.h"
#include "3d/CCSprite3D.h"
#include "renderer/CCTextureCache.h"

NS_CC_BEGIN

ParticleAssetCreator* ParticleAssetCreator::s_sharedCreator = nullptr;

ParticleAssetCreator* ParticleAssetCreator::getInstance()
{
    if (!s_sharedCreator)
    {
        s_sharedCreator = new (std::nothrow) ParticleAssetCreator();
    }
    return s_sharedCreator;
}

void ParticleAssetCreator::destroyInstance()
{
    CC_SAFE_RELEASE_NULL(s_sharedCreator);
}


void ParticleAssetCreator::setDelegate(AssetCreatorDelegate* delegate)
{
    m_delegate = delegate;
}


std::string ParticleAssetCreator::getSprite3DFilename(const std::string& assetName)
{
    if (m_delegate)
    {
        return m_delegate->getSprite3DFilename(assetName);
    }
    
    return assetName;
}

Texture2D* ParticleAssetCreator::createTexture(const std::string& assetName)
{
    Texture2D * retVal;
    cocos2d::Image::setPNGPremultipliedAlphaEnabled(true);
    if (m_delegate)
    {
        retVal =  m_delegate->createTexture(assetName);
    } else {
        retVal = Director::getInstance()->getTextureCache()->addImage(assetName);
    }
    
    
    cocos2d::Image::setPNGPremultipliedAlphaEnabled(false);
    return retVal;
}

Sprite3D* ParticleAssetCreator::createSprite3D(const std::string& assetName)
{
    if (m_delegate)
    {
        return m_delegate->createSprite3D(assetName);
    }
    
    return Sprite3D::create(assetName);
}

//    virtual void createShaders() = 0;


NS_CC_END
