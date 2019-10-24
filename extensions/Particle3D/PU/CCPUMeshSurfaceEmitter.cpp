/****************************************************************************
 Copyright (C) 2013 Henry van Merode. All rights reserved.
 Copyright (c) 2015-2016 Chukong Technologies Inc.
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

#include "3d/CCSprite3D.h"
#include "3d/CCBundle3D.h"
#include "platform/CCFileUtils.h"

#include "CCPUMeshSurfaceEmitter.h"
#include "extensions/Particle3D/ParticleAssetCreator.h"
#include "extensions/Particle3D/PU/CCPUParticleSystem3D.h"
#include "extensions/Particle3D/PU/CCPUUtil.h"

NS_CC_BEGIN
// Constants
const Vec3 PUMeshSurfaceEmitter::DEFAULT_SCALE(1, 1, 1);
const MeshInfo::MeshSurfaceDistribution PUMeshSurfaceEmitter::DEFAULT_DISTRIBUTION = MeshInfo::MSD_HOMOGENEOUS;

//-----------------------------------------------------------------------
inline void PUTriangle::calculateSquareSurface ()
{
    /* Calculating the surface of a triangle with the following algorithm:
        v1 = Vector3(x1, y1, z1)
        v2 = Vector3(x2, y2, z2)
        v3 = Vector3(x3, y3, z3)
        a = sqrt ((x1-x3)*(x1-x3) + (y1-y3)*(y1-y3) + (z1-z3)*(z1-z3))
        b = sqrt ((x2-x1)*(x2-x1) + (y2-y1)*(y2-y1) + (z2-z1)*(z2-z1))
        c = sqrt ((x3-x2)*(x3-x2) + (y3-y2)*(y3-y2) + (z3-z2)*(z3-z2))
        p = 0.5 * (a + b + c)
        surface = sqrt (p * (p-a) * (p-b) * (p-c));
    */

    // a, b and c are the length of each triangle
    float a = sqrt (	(v1.x - v3.x) * (v1.x - v3.x) + 
                        (v1.y - v3.y) * (v1.y - v3.y) + 
                        (v1.z - v3.z) * (v1.z - v3.z));
    float b = sqrt (	(v2.x - v1.x) * (v2.x - v1.x) + 
                        (v2.y - v1.y) * (v2.y - v1.y) + 
                        (v2.z - v1.z) * (v2.z - v1.z));
    float c = sqrt (	(v3.x - v2.x) * (v3.x - v2.x) + 
                        (v3.y - v2.y) * (v3.y - v2.y) + 
                        (v3.z - v2.z) * (v3.z - v2.z));
    float p = 0.5f * (a + b + c);

    // Assign square surface of the triangle
    squareSurface = p * (p-a) * (p-b) * (p-c);
}
//-----------------------------------------------------------------------
inline void PUTriangle::calculateSurfaceNormal ()
{
    /* Calculating the surface normal of a triangle with the following algorithm:
        v1 = Vector3(x1, y1, z1)
        v2 = Vector3(x2, y2, z2)
        v3 = Vector3(x3, y3, z3)
        n = (v2-v1)x(v3-v1), where the 'x' is the cross product
    */
    Vec3::cross(v2-v1, v3-v1, &surfaceNormal);
    surfaceNormal.normalize();
}
//-----------------------------------------------------------------------
inline void PUTriangle::calculateEdgeNormals ()
{
    Vec3::cross(v1, v2, &en1);
    en1.normalize();
    Vec3::cross(v2, v3, &en2);
    en2.normalize();
    Vec3::cross(v3, v1, &en3);
    en3.normalize();
}
//-----------------------------------------------------------------------
const Vec3 PUTriangle::getRandomTrianglePosition ()
{
    // Use barycentric coordinates. Let A, B, C be the three vertices of the triangle. Any point P inside can 
    // be expressed uniquely as P = aA + bB + cC, where a+b+c=1 and a,b,c are each >= 0.
    // Knowing a and b permits you to calculate c=1-a-b.
    // So if you can generate two random numbers a and b, each in [0,1], such that their sum <=1, you've got a 
    // random point in your triangle. Generate random a and b independently and uniformly in [0,1] 
    // If a+b>1, replace a by 1-a, b by 1-b. Let c=1-a-b. Then aA + bB + cC is uniformly distributed 
    // in triangle ABC: the reflection step a=1-a; b=1-b gives a point (a,b) uniformly distributed in the 
    // triangle (0,0)(1,0)(0,1), which is then mapped affinely to ABC. Now you have barycentric coordinates 
    // a,b,c. Compute your point P = aA + bB + cC.
    float a = CCRANDOM_0_1();
    float b = CCRANDOM_0_1();
    if (a + b > 1)
    {
        a = 1 - a;
        b = 1 - b;
    }
    float c = 1 - a - b;
    return a * v1 + b * v2 + c * v3;
}
//-----------------------------------------------------------------------
const PUTriangle::PositionAndNormal PUTriangle::getRandomEdgePositionAndNormal ()
{
    float mult = CCRANDOM_0_1();
    float randomVal = CCRANDOM_0_1() * 3.0f;
    PositionAndNormal pAndN;
    pAndN.position.setZero();
    pAndN.normal.setZero();
    if (randomVal < 1)
    {
        pAndN.position.set(v2.x + mult*(v1.x - v2.x), v2.y + mult*(v1.y - v2.y), v2.z + mult*(v1.z - v2.z));
        pAndN.normal = en1;
    }
    else
    {
        if (randomVal < 2)
        {
            pAndN.position.set(v3.x + mult*(v2.x - v3.x), v3.y + mult*(v2.y - v3.y), v3.z + mult*(v2.z - v3.z));
            pAndN.normal = en2;
        }
        else
        {
    
            pAndN.position.set(v1.x + mult*(v3.x - v1.x), v1.y + mult*(v3.y - v1.y),  v1.z + mult*(v3.z - v1.z));
            pAndN.normal = en3;
        }
    }

    return pAndN;
}
//-----------------------------------------------------------------------
const PUTriangle::PositionAndNormal PUTriangle::getRandomVertexAndNormal ()
{
    float randomVal = CCRANDOM_0_1() * 3.0f;
    PositionAndNormal pAndN;
    pAndN.position.setZero();
    pAndN.normal.setZero();

    if (randomVal < 1)
    {
        pAndN.position = v1;
        pAndN.normal = vn1;
    }
    else
    {
        if (randomVal < 2)
        {
            pAndN.position = v2;
            pAndN.normal = vn2;
        }
        else
        {
            pAndN.position = v3;
            pAndN.normal = vn3;
        }
    }

    return pAndN;
}
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
MeshInfo::MeshInfo (const std::string& meshName,
    MeshSurfaceDistribution distribution,
    const Quaternion& orientation,
    const Vec3& scale) :
    mDistribution(distribution)
{
    std::string meshFile = ParticleAssetCreator::getInstance()->getSprite3DFilename(meshName);
    std::string fullpath = FileUtils::getInstance()->fullPathForFilename(meshFile);
    auto bundle = Bundle3D::createBundle();
    if (!bundle->load(fullpath)) {
        CCLOG("Couldn't find c3b file for mesh emitter '%s'", meshFile.c_str());
        Bundle3D::destroyBundle(bundle);
        return;
    }
    
    auto meshdatas = std::make_shared<MeshDatas>();
    if (!bundle->loadMeshDatas(*meshdatas)) {
        CCLOG("Failed to load mesh emitter '%s'", meshFile.c_str());
        Bundle3D::destroyBundle(bundle);
        return;
    }
    
    getMeshInformation(*meshdatas, Vec3::ZERO, orientation, scale);
    Bundle3D::destroyBundle(bundle);
}
//-----------------------------------------------------------------------
MeshInfo::~MeshInfo ()
{
    _triangles.clear();
}
//-----------------------------------------------------------------------
inline float MeshInfo::getGaussianRandom (float high, float cutoff)
{
    float x1 = 0;
    float x2 = 0;
    float w = 0;
    float y1 = 0;

    unsigned int max = 0;
    do
    {
        x1 = CCRANDOM_0_1();
        x2 = CCRANDOM_0_1();
        w = x1 * x1 + x2 * x2;

        // Prevent infinite loop
        if (w >= 1.0f && max > 4 )
            w = x1;
        
    } while (w >= 1.0f);
    
    w = sqrt((-2.0f * ::log(w)) / w);
    y1 = std::abs(x1 * w);
    y1 = y1 > cutoff ? cutoff : y1;
    y1 *= high / cutoff;
    return y1;
}

//-----------------------------------------------------------------------
const PUTriangle& MeshInfo::getTriangle (size_t triangleIndex)
{
    return _triangles[triangleIndex];
}

//-----------------------------------------------------------------------
size_t MeshInfo::getRandomTriangleIndex()
{
    size_t index;
    if (mDistribution == MSD_HOMOGENEOUS || mDistribution == MSD_HETEROGENEOUS_1)
    {
        index = (size_t)getGaussianRandom((float)_triangles.size() - 1);
    }
    else
        index = (size_t)(CCRANDOM_0_1() * (float)(_triangles.size() - 1));

    return index;
}

//-----------------------------------------------------------------------
const PUTriangle::PositionAndNormal MeshInfo::getRandomPositionAndNormal (const size_t triangleIndex)
{
    PUTriangle triangle = getTriangle(triangleIndex);
    PUTriangle::PositionAndNormal pAndN;
    pAndN.position.setZero();
    pAndN.normal.setZero();
    if (mDistribution == MSD_HOMOGENEOUS || mDistribution == MSD_HETEROGENEOUS_1 || mDistribution == MSD_HETEROGENEOUS_2)
    {
        pAndN.position = triangle.getRandomTrianglePosition();
        pAndN.normal = triangle.surfaceNormal;
    }
    else
    {
        if (mDistribution == MSD_VERTEX)
        {
            pAndN = triangle.getRandomVertexAndNormal();
        }
        else
        {
            if (mDistribution == MSD_EDGE)
            {
                pAndN = triangle.getRandomEdgePositionAndNormal();
            }
        }
    }
    return pAndN;
}

//-----------------------------------------------------------------------
void MeshInfo::getMeshInformation(	const MeshDatas& meshes,
									const Vec3 &position,
									const Quaternion &orient,
									const Vec3 &scale)
{
	size_t vertexCount = 0;
	size_t indexCount = 0;
	Vec3* vertices = 0;
	Vec3* normals = 0;
	unsigned long* indices = 0;

	// Calculate how many vertices and indices we're going to need
	for ( unsigned short i = 0; i < meshes.meshDatas.size(); ++i)
	{
		MeshData* submesh = meshes.meshDatas.at( i );

        int floatVertDataSize = submesh->vertex.size();
        int sizePerVert = submesh->getPerVertexSize();
        int numVertices = (floatVertDataSize * sizeof(float)) / sizePerVert;
        vertexCount += numVertices;

		// Add the indices
        for (const auto& indexBuffer : submesh->subMeshIndices)
        {
            indexCount += indexBuffer.size();
        }
	}

	// Allocate space for the vertices and indices
	vertices = new (std::nothrow) Vec3[vertexCount];
	normals = new (std::nothrow) Vec3[vertexCount];
	indices = new (std::nothrow) unsigned long[indexCount];
    size_t vertIndexOffset = 0, indexIndex = 0;
	// Run through the submeshes again, adding the data into the arrays
	for ( unsigned short i = 0; i < meshes.meshDatas.size(); ++i)
	{
		MeshData* submesh = meshes.meshDatas.at( i );
        if (submesh->vertex.empty())
            continue;
        
        int floatVertDataSize = submesh->vertex.size();
        int sizePerVert = submesh->getPerVertexSize();
        int numVertices = (floatVertDataSize * sizeof(float)) / sizePerVert;
        
        //Figure out offsets into the vertex data for position/normal.
        int posFloatOffset = 0, normFloatOffset = 0;
        bool hitPos = false, hitNorm = false;
        for (const auto& attrib : submesh->attribs)
        {
            if (attrib.vertexAttrib == shaderinfos::VertexKey::VERTEX_ATTRIB_POSITION)
                hitPos = true;
            if (attrib.vertexAttrib == shaderinfos::VertexKey::VERTEX_ATTRIB_NORMAL)
                hitNorm = true;
            
            if (!hitPos)
                posFloatOffset += attrib.getAttribSizeBytes() / sizeof(float);
            if (!hitNorm)
                normFloatOffset += attrib.getAttribSizeBytes() / sizeof(float);
        }
        
        float* curVert = &submesh->vertex[0];
        float* curPosition = nullptr, *curNormal = nullptr;
        int vertFloatStride = sizePerVert / sizeof(float);
        for( size_t j = 0; j < numVertices; ++j, curVert += vertFloatStride)
        {
            curPosition = curVert + posFloatOffset;
            Vec3 pos(curPosition[0] * scale.x, curPosition[1] * scale.y, curPosition[2] * scale.z);
            vertices[vertIndexOffset + j] = (orient * pos) + position;

            curNormal = curVert + normFloatOffset;
            Vec3 norm(curNormal[0], curNormal[1], curNormal[2]);
            normals[vertIndexOffset + j] = norm;
        }

        for (const auto& indexBuffer : submesh->subMeshIndices)
        {
            const unsigned short* curIndex = &indexBuffer.at(0);
            for ( size_t k = 0; k < indexBuffer.size(); ++k)
            {
                indices[indexIndex++] = static_cast<unsigned long>(curIndex[k]) + static_cast<unsigned long>(vertIndexOffset);
            }
        }
        
        vertIndexOffset += numVertices;
	}

	// Create triangles from the retrieved data
    _triangles.resize(indexCount / 3);
    size_t curTriangle = 0;
	for (size_t k = 0; k < indexCount-1; k+=3)
	{
		PUTriangle& t = _triangles[curTriangle++];
		t.v1 = vertices [indices[k]];
		t.vn1 = normals [indices[k]];

		t.v2 = vertices [indices[k+1]];
		t.vn2 = normals [indices[k+1]];

		t.v3 = vertices [indices[k+2]];
		t.vn3 = normals [indices[k+2]];

		t.calculateSquareSurface();
		t.calculateSurfaceNormal();
		t.calculateEdgeNormals();
		_triangles.push_back(t);
	}

	// Delete intermediate arrays
	delete [] indices;
	delete [] normals;
	delete [] vertices;

	// Sort the triangle on their size, if needed (only if a gaussian random number generator
	// function is used to perform a random lookup of a triangle)
	if (mDistribution == MSD_HOMOGENEOUS)
		sort(_triangles.begin(), _triangles.end(), PUSortDescending());
	else
		if (mDistribution == MSD_HETEROGENEOUS_1)
			sort(_triangles.begin(), _triangles.end(), PUSortAscending());
}
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
//-----------------------------------------------------------------------
PUMeshSurfaceEmitter::PUMeshSurfaceEmitter() : 
    PUEmitter(),
    _meshName(),
    _orientation(),
    _scale(DEFAULT_SCALE),
    _distribution(DEFAULT_DISTRIBUTION),
    _meshInfo(0),
    _triangleIndex(0),
    _directionSet(false)
{
}
//-----------------------------------------------------------------------
PUMeshSurfaceEmitter::~PUMeshSurfaceEmitter()
{
    if (_meshInfo)
    {
        CC_SAFE_DELETE(_meshInfo);
    }
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::prepare()
{
    PUEmitter::prepare();
    // Build the data
    if (!_meshName.empty())
    {
        build();
    }
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::unPrepare()
{
    // Todo
    PUEmitter::unPrepare();
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::initParticlePosition(PUParticle3D* particle)
{
    PUTriangle::PositionAndNormal pAndN;
    pAndN.position.setZero();
    pAndN.normal.setZero();
    _directionSet = false;

    if (_meshInfo && _meshInfo->getTriangleCount())
    {
        getDerivedPosition();
        _triangleIndex = _meshInfo->getRandomTriangleIndex (); // Get a random triangle index
        pAndN = _meshInfo->getRandomPositionAndNormal(_triangleIndex); // Determine position and normal
        //ParticleSystem* sys = mParentTechnique->getParentSystem();
        if (_autoDirection)
        {
            if (pAndN.normal != Vec3::ZERO)
            {
                // Set position and direction of the particle
                //if (sys)
                {
                    Mat4 rotMat;
                    Mat4::createRotation(static_cast<PUParticleSystem3D *>(_particleSystem)->getDerivedOrientation(), &rotMat);
                    
                    Vec3 basePos = getDerivedPosition();
                    basePos = basePos - (_latestPositionDiff * particle->m_spawnT);
                    
                    particle->position = basePos + rotMat * Vec3(_emitterScale.x * pAndN.position.x, _emitterScale.y * pAndN.position.y, _emitterScale.z * pAndN.position.z);
                }
                //else
                //{
                //	particle->position = _derivedPosition + _emitterScale * pAndN.position;
                //}

                // The value of the direction vector that has been set does not have a meaning
                float angle = (_dynamicAttributeHelper.calculate(_dynAngle, (static_cast<PUParticleSystem3D *>(_particleSystem))->getTimeElapsedSinceStart()));
                if (angle != 0.0f)
                {
                    //particle->direction = (pAndN.normal).randomDeviant(angle, mUpVector);
                    particle->direction = PUUtil::randomDeviant(pAndN.normal, angle, _upVector);
                }
                else
                {
                    particle->direction = pAndN.normal;
                }

                particle->originalDirection = particle->direction;
                _directionSet = true;
            }
        }
        else
        {
            // Set position of the particle
            //if (sys)
            {
                Mat4 rotMat;
                Mat4::createRotation(static_cast<PUParticleSystem3D *>(_particleSystem)->getDerivedOrientation(), &rotMat);
                
                Vec3 basePos = getDerivedPosition();
                basePos = basePos - (_latestPositionDiff * particle->m_spawnT);
                
                particle->position = basePos + rotMat * Vec3(_emitterScale.x * pAndN.position.x, _emitterScale.y * pAndN.position.y, _emitterScale.z * pAndN.position.z);
            }
            //else
            //{
            //	particle->position = mDerivedPosition + _mEmitterScale * pAndN.position;
            //}
        }
    
        particle->originalPosition = particle->position;
    }
}
//-----------------------------------------------------------------------
unsigned short PUMeshSurfaceEmitter::calculateRequestedParticles(float timeElapsed)
{
    if (_meshInfo)
    {
        return PUEmitter::calculateRequestedParticles(timeElapsed);
    }

    return 0;
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::initParticleDirection(PUParticle3D* particle)
{
    // Only determine direction if it hasn't been calculated yet
    if (!_directionSet)
    {
        PUEmitter::initParticleDirection(particle);
    }
}
//-----------------------------------------------------------------------
const std::string& PUMeshSurfaceEmitter::getMeshName() const
{
    return _meshName;
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::setMeshName(const std::string& meshName, bool doBuild)
{
    _meshName = meshName;

    // If needed, build the data needed for emitting particles
    if (doBuild)
    {
        build();
    }
}
//-----------------------------------------------------------------------
MeshInfo::MeshSurfaceDistribution PUMeshSurfaceEmitter::getDistribution() const
{
    return _distribution;
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::setDistribution(MeshInfo::MeshSurfaceDistribution distribution)
{
    _distribution = distribution;
}
//-----------------------------------------------------------------------
const Vec3& PUMeshSurfaceEmitter::getScale () const
{
    return _scale;
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::setScale (const Vec3& scale)
{
    _scale = scale;
}
//-----------------------------------------------------------------------
void PUMeshSurfaceEmitter::build()
{
    // Delete the mesh info if already existing
    if (_meshInfo)
    {
        CC_SAFE_DELETE(_meshInfo);
    }

    // Generate meshinfo.
    _meshInfo = new (std::nothrow) MeshInfo(_meshName, _distribution, _orientation, _scale);
}

PUMeshSurfaceEmitter* PUMeshSurfaceEmitter::create()
{
    auto pe = new (std::nothrow) PUMeshSurfaceEmitter();
    pe->autorelease();
    return pe;
}

void PUMeshSurfaceEmitter::copyAttributesTo( PUEmitter* emitter )
{
    PUEmitter::copyAttributesTo(emitter);
    PUMeshSurfaceEmitter* meshSurfaceEmitter = static_cast<PUMeshSurfaceEmitter*>(emitter);
    meshSurfaceEmitter->_meshName = _meshName;
    meshSurfaceEmitter->_distribution = _distribution;
    meshSurfaceEmitter->_orientation = _orientation;
    meshSurfaceEmitter->_scale = _scale;
    meshSurfaceEmitter->_meshInfo = new (std::nothrow) MeshInfo(_meshName, _distribution, _orientation, _scale);
}

PUMeshSurfaceEmitter* PUMeshSurfaceEmitter::clone()
{
    auto be = PUMeshSurfaceEmitter::create();
    copyAttributesTo(be);
    return be;
}

NS_CC_END
