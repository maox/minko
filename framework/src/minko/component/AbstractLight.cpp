/*
Copyright (c) 2013 Aerys

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "AbstractLight.hpp"

#include "minko/scene/Node.hpp"
#include "minko/component/LightManager.hpp"

using namespace minko;
using namespace minko::component;

AbstractLight::AbstractLight(const std::string& arrayName, uint lightId) :
	AbstractRootDataComponent(data::ArrayProvider::create(arrayName, lightId)),
	_priority(0.f),
	_color(math::Vector3::create(1, 1, 1)),
	_arrayData(std::dynamic_pointer_cast<data::ArrayProvider>(data()))
{
	data()->set("color", _color);
}

void
AbstractLight::updateRoot(scene::Node::Ptr node)
{
	auto oldRoot = root();

	AbstractRootDataComponent::updateRoot(node);

	auto newRoot = root();

	if (newRoot != oldRoot)
	{
		auto lightManager = oldRoot ? oldRoot->component<LightManager>() : nullptr;

		if (lightManager)
			oldRoot->removeComponent(lightManager);
		else
			lightManager = LightManager::create();

		if (newRoot && !newRoot->hasComponent<LightManager>())
			newRoot->addComponent(lightManager);
	}
}
