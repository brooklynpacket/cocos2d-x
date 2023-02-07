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

#include "3d/CCAttachNode.h"
#include "3d/CCSkeleton3D.h"

NS_CC_BEGIN

//START BPC PATCH
AttachNode* AttachNode::create(Bone3D* attachBone, bool ignoreParentScale /*=false*/)
{
    auto attachnode = new (std::nothrow) AttachNode();
    attachnode->_attachBone = attachBone;
    attachnode->autorelease();
    attachnode->_ignoreParentScale = ignoreParentScale;
    
    return attachnode;
}
//END BPC PATCH

AttachNode::AttachNode()
: _attachBone(nullptr)
{
    
}
AttachNode::~AttachNode()
{
    
}

Mat4 AttachNode::getWorldToNodeTransform() const
{
    static Mat4 mat;
    mat.setIdentity();
    auto parent = getParent();
    if (parent)
    {
        mat = parent->getWorldToNodeTransform() * _attachBone->getWorldMat() * Node::getNodeToParentTransform();
    }
    else
    {
        mat = _attachBone->getWorldMat() * Node::getNodeToParentTransform();
    }
    return mat;
}

Mat4 AttachNode::getNodeToWorldTransform() const
{
    return Node::getNodeToWorldTransform();
}

const Mat4& AttachNode::getNodeToParentTransform() const
{
    Node::getNodeToParentTransform();
    //START BPC PATCH
    Mat4 parent = _attachBone->getWorldMat();
    if (_ignoreParentScale) {
        // Recalculate parent scale to be 1
        Vec3 col1 = {parent.m[0], parent.m[4], parent.m[8]};
        Vec3 col2 = {parent.m[1], parent.m[5], parent.m[9]};
        Vec3 col3 = {parent.m[2], parent.m[6], parent.m[10]};
        col1.normalize();
        col2.normalize();
        col3.normalize();
        
        parent.m[0] = col1.x;
        parent.m[4] = col1.y;
        parent.m[8] = col1.z;
        
        parent.m[1] = col2.x;
        parent.m[5] = col2.y;
        parent.m[9] = col2.z;
        
        parent.m[2] = col3.x;
        parent.m[6] = col3.y;
        parent.m[10] = col3.z;
    }
    _transformToParent = parent * _transform;
    //END BPC PATCH
    return _transformToParent;
}

void AttachNode::visit(Renderer *renderer, const Mat4& parentTransform, uint32_t parentFlags)
{
    Node::visit(renderer, parentTransform, parentFlags | Node::FLAGS_DIRTY_MASK);
}
NS_CC_END

