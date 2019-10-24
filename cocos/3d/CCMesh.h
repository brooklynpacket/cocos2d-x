/****************************************************************************
 Copyright (c) 2014-2016 Chukong Technologies Inc.
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
#pragma once

#include <string>
#include <map>

#include "3d/CCBundle3DData.h"
#include "3d/CCAABB.h"
#include "3d/CC3DProgramInfo.h"

#include "base/CCRef.h"
#include "math/CCMath.h"
#include "renderer/CCMeshCommand.h"
#include "renderer/CCCustomCommand.h"

NS_CC_BEGIN

/**
 * @addtogroup _3d
 * @{
 */

class Texture2D;
class TextureCube;
class MeshSkin;
class MeshIndexData;
class Material;
class Renderer;
class Scene;
class Pass;


/*BPC PATCH*/
enum class GLWriteMode{
    Default,  //based on transparency
    AlwaysOn,
    AlwaysOff,
};
/*END BPC PATCH*/
 
namespace backend
{
    class Buffer;
}

/** 
 * @brief Mesh: contains ref to index buffer, GLProgramState, texture, skin, blend function, aabb and so on
 */
class CC_DLL Mesh : public Ref
{
    friend class Sprite3D;
public:
    typedef std::vector<unsigned short> IndexArray;
    /**create mesh from positions, normals, and so on, single SubMesh*/
    static Mesh* create(const std::vector<float>& positions, const std::vector<float>& normals, const std::vector<float>& texs, const IndexArray& indices);
    
    /**
     * @lua NA
     */
    static Mesh* create(const std::vector<float>& vertices, int perVertexSizeInFloat, const IndexArray& indices, const std::vector<MeshVertexAttrib>& attribs);
    
    /** 
     * create mesh
     * @lua NA
     */
    static Mesh* create(const std::string& name, MeshIndexData* indexData, MeshSkin* skin = nullptr);
    
    /**
     * get vertex buffer
     * 
     * @lua NA
     */
    backend::Buffer* getVertexBuffer() const;
    /**
     * has vertex attribute?
     *
     * @lua NA
     */
    bool hasVertexAttrib(shaderinfos::VertexKey attrib) const;
    /**get mesh vertex attribute count*/
    ssize_t getMeshVertexAttribCount() const;
    /**get MeshVertexAttribute by index*/
    const MeshVertexAttrib& getMeshVertexAttribute(int idx);
    /**get per vertex size in bytes*/
    int getVertexSizeInBytes() const;

    /**
     * set texture (diffuse), which is responsible for the main appearance. It is also means main texture, you can also call setTexture(texPath, NTextureData::Usage::Diffuse)
     * @param texPath texture path
     */
    void setTexture(const std::string& texPath);
    /**
     * set texture (diffuse), which is responsible for the main appearance. It is also means main texture, you can also call setTexture(texPath, NTextureData::Usage::Diffuse)
     * @param tex texture to be set
     */
    void setTexture(Texture2D* tex);
    /**
     * set texture
     * @param tex texture to be set
     * @param usage Usage of this texture
     * @param whether refresh the cache file name
     */
    void setTexture(Texture2D* tex, NTextureData::Usage usage,bool cacheFileName = true);
    /**
     * set texture
     * @param texPath texture path
     * @param usage Usage of this texture
     */
    void setTexture(const std::string& texPath, NTextureData::Usage usage);
    
    //BPC PATCH
    /**
     * set texture
     * @param tex texture to be set
     * @param usage Usage of this texture
     */
    void setTextureCube(TextureCube* tex, NTextureData::Usage usage);
    //END BPC PATCH
    
    /**
     * Get texture (diffuse), which is responsible for the main appearance. It is also means main texture, you can also call getTexture(NTextureData::Usage::Diffuse)
     * @return Texture used, return the texture of first mesh if multiple meshes exist
     */
    Texture2D* getTexture() const;
    /**
     * Get texture
     * @param usage Usage of returned texture
     * @return The texture of this usage, return the texture of first mesh if multiple meshes exist
     */
    Texture2D* getTexture(NTextureData::Usage usage);
    
    /**visible getter and setter*/
    void setVisible(bool visible);
    bool isVisible() const;
    
    /**
     * skin getter
     *
     * @lua NA
     */
    MeshSkin* getSkin() const { return _skin; }
    
    /**
     * mesh index data getter
     *
     * @lua NA
     */
    MeshIndexData* getMeshIndexData() const { return _meshIndexData; }
    
    /**
     * get ProgramState
     * 
     * @lua NA
     */
    backend::ProgramState* getProgramState() const;
    
    /**name getter */
    const std::string& getName() const { return _name; }
    
    void setBlendFunc(const BlendFunc &blendFunc);
    const BlendFunc &getBlendFunc() const;
    
    /** 
     * get primitive type
     *
     * @lua NA
     */
    CustomCommand::PrimitiveType getPrimitiveType() const;
    /**
     * get index count
     *
     * @lua NA
     */
    ssize_t getIndexCount() const;
    /**
     * get index format
     *
     * @lua NA
     */
    CustomCommand::IndexFormat getIndexFormat() const;
    /**
     * get index buffer
     *
     * @lua NA
     */
    backend::Buffer* getIndexBuffer() const;
    
    /**get AABB*/
    const AABB& getAABB() const { return _aabb; }
    const AABB& getSkinnedAABB() const { return m_skinnedAABB; }

    /**  Sets a new ProgramState for the Mesh
     * A new Material will be created for it
     */
    void setProgramState(backend::ProgramState* programState);

    /** Sets a new Material to the Mesh */
    void setMaterial(Material* material);

    /** Returns the Material being used by the Mesh */
    Material* getMaterial() const;

    void draw(Renderer* renderer, float globalZ, const Mat4& transform, uint32_t flags, unsigned int lightMask, const Vec4& color, bool forceDepthWrite);

    /**skin setter*/
    void setSkin(MeshSkin* skin);
    /**Mesh index data setter*/
    void setMeshIndexData(MeshIndexData* indexdata);
    /**name setter*/
    void setName(const std::string& name) { _name = name; }
 
    /** 
     * calculate the AABB of the mesh
     * @note the AABB is in the local space, not the world space
     */
    void calculateAABB();
    
    /**
     * force set this Sprite3D to 2D render queue
     */
    void setForce2DQueue(bool force2D) { _force2DQueue = force2D; }

    std::string getTextureFileName(){ return _texFile; }

    
    /*BPC-PATCH, some of these might want to migrate down into CCTechnique*/
    float getGlobalZ() const {return m_globalZ;}
    void setGlobalZ(float zVal) {m_globalZ = zVal;}
    void setIsTransparent(bool transparent) { _isTransparent = transparent;}
    bool getIsTransparent() const {return _isTransparent;}
    GLWriteMode getDepthWriteMode() const {return m_depthWriteMode;}
    void setDepthWriteMode(GLWriteMode mode) {m_depthWriteMode = mode;}
    GLWriteMode getCullFaceMode() const {return m_cullFaceMode;}
    void setCullFaceMode(GLWriteMode mode){m_cullFaceMode = mode;}
    bool getIsShadowCaster() const {return m_isShadowCaster;}
    bool getIsShadowReceiver() const {return m_isShadowReceiver;}
    bool getIsVertexLit() const {return m_isVertexLit;}
    bool getReceiveFog() const {return m_receiveFog;}
    bool getSkipRender() const {return m_skipRender;}
    void setIsShadowCaster(bool cast) {m_isShadowCaster = cast;}
    void setIsShadowReceiver(bool receive) {m_isShadowReceiver = receive;}
    void setIsVertexLit(bool vertexLit) {m_isVertexLit = vertexLit;}
    void setReceiveFog(bool fog) { m_receiveFog = fog; }
    void setSkipRender(bool skip) {m_skipRender = skip;}
    void setSkinnedAABB(const AABB& skinnedBB);
    void setUseMeshDepth(bool use) { m_useMeshDepth = use; }
    void setLocalSortOrder(int order) {m_localDepthSort = order;}
    int getLocalSortORder() const {return m_localDepthSort;}
    
    bool boolFromWriteMode(GLWriteMode mode) const{
        switch(mode){
            case GLWriteMode::Default:
                return !getIsTransparent();
            case GLWriteMode::AlwaysOn:
                return true;
            case GLWriteMode::AlwaysOff:
                return false;
        }
    }
    
    void setMeshLightMask(unsigned int mask) { m_meshLightMask = mask; }
    unsigned int getMeshLightMask() const { return m_meshLightMask; }
    
    void addCommandOverride(const std::string& techniqueName);
    
    void setPointLightCount(int count);
    void setFxPointLightCount(int count);
    void setDirLightCount(int count);
    void setSpotLightCount(int count);
    void setFxSpotLightCount(int count);
    void setHasAmbientLight(bool hasAmbient) { m_hasAmbientComponent = hasAmbient; }
    bool hasAmbientLight() const { return m_hasAmbientComponent; }
    int getPointLightCount();
    int getFxPointLightCount();
    int getDirLightCount();
    int getSpotLightCount();
    int getFxSpotLightCount();
    
    MeshCommand * getMeshCommandForTechniqueAndPass(Technique * technique, int pass);
    /*END BPC-PATCH*/
    
    
CC_CONSTRUCTOR_ACCESS:

    Mesh();
    virtual ~Mesh();

protected:
    void resetLightUniformValues();
    void setLightUniforms(Pass* pass, Scene* scene, const Vec4& color, unsigned int lightmask);
    void bindMeshCommand();

    std::map<NTextureData::Usage, Texture2D*> _textures; //textures that submesh is using
    //BPC PATCH
    std::map<NTextureData::Usage, TextureCube*> _textureCubes; //cube map textures that submesh is using
    //END BPC PATCH
    MeshSkin*           _skin;     //skin
    bool                _visible; // is the submesh visible
    bool                _isTransparent; // is this mesh transparent, it is a property of material in fact
    bool                _force2DQueue; // add this mesh to 2D render queue
    
    std::string         _name;
    MeshIndexData*      _meshIndexData;
    //GLProgramState*     _glProgramState;
    BlendFunc           _blend;
    bool                _blendDirty;
    Material*           _material;
    AABB                _aabb;
    std::function<void()>       _visibleChanged;
    std::unordered_map<std::string, std::vector<MeshCommand> >    _meshCommands;
    
    
    /*BPC-PATCH*/
    GLWriteMode m_depthWriteMode{GLWriteMode::Default};
    GLWriteMode m_cullFaceMode{GLWriteMode::Default};
    float m_globalZ{std::numeric_limits<float>::max()};
    AABB m_skinnedAABB;
    bool m_useMeshDepth{true};
    
    //Sometimes we want to render a mesh with different techniques in a single render pass. Renderer has a pointer to the MeshCommand, so for these instances we need to specify a different command.
    std::map<std::string, MeshCommand> m_techniqueToCommandOverrides;
    /*END BPC-PATCH*/
    
    
    ///light parameters
    /*BPC PATCH*/
    int m_pointLightCount = -1;
    int m_dirLightCount = -1;
    int m_spotLightCount = -1;
    int m_fxPointLightCount = 0;
    int m_fxSpotLightCount = 0;
    unsigned int m_meshLightMask = 0;
    bool m_hasAmbientComponent = false;
    bool m_isShadowCaster = false;
    bool m_isShadowReceiver = false;
    bool m_isVertexLit = false;
    bool m_receiveFog = false;
    bool m_skipRender = false;
    int m_localDepthSort = -1;
    /*END BPC PATCH*/
    
    std::vector<Vec3> _dirLightUniformColorValues;
    std::vector<Vec3> _dirLightUniformDirValues;
    
    std::vector<Vec3> _pointLightUniformColorValues;
    std::vector<Vec3> _pointLightUniformPositionValues;
    std::vector<float> _pointLightUniformRangeInverseValues;
    
    std::vector<Vec3> _spotLightUniformColorValues;
    std::vector<Vec3> _spotLightUniformPositionValues;
    std::vector<Vec3> _spotLightUniformDirValues;
    std::vector<float> _spotLightUniformInnerAngleCosValues;
    std::vector<float> _spotLightUniformOuterAngleCosValues;
    std::vector<float> _spotLightUniformRangeInverseValues;

    std::string _texFile;
};

// end of 3d group
/// @}

/// @cond
extern std::string CC_DLL s_uniformSamplerName[];//uniform sampler names array
/// @endcond

NS_CC_END
