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

#include "Surface.hpp"

#include "minko/scene/Node.hpp"
#include "minko/geometry/Geometry.hpp"
#include "minko/render/Effect.hpp"
#include "minko/render/DrawCall.hpp"
#include "minko/render/Pass.hpp"
#include "minko/render/Program.hpp"
#include "minko/data/Container.hpp"

using namespace minko;
using namespace minko::data;
using namespace minko::component;
using namespace minko::geometry;
using namespace minko::render;

Surface::Surface(Geometry::Ptr 			geometry,
				 data::Provider::Ptr 	material,
				 Effect::Ptr			effect,
				 const std::string&		technique) :
	AbstractComponent(),
	_geometry(geometry),
	_material(material),
	_effect(effect),
	_technique(technique),
	_techniqueMacroNames(),
	_drawCalls(),
	_drawCallToPass(),
	_macroPropertyNameToDrawCalls(),
	_macroAddedOrRemovedSlots(),
	_macroChangedSlots(),
	_numMacroListeners(),
	_incorrectMacroToPasses(),
	_incorrectMacroChangedSlot(),
	_drawCallAdded(DrawCallChangedSignal::create()),
	_drawCallRemoved(DrawCallChangedSignal::create()),
	_techniqueChanged(TechniqueChangedSignal::create())
{
}

void
Surface::initialize()
{
	_targetAddedSlot = targetAdded()->connect(std::bind(
		&Surface::targetAddedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
	));

	_targetRemovedSlot = targetRemoved()->connect(std::bind(
		&Surface::targetRemovedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2
	));

	auto techniques = _effect->techniques();

	if (techniques.count(_technique) == 0)
		throw std::logic_error("The technique '" + _technique + "' does not exist.");

	initializeTechniqueMacroNames();

}

void
Surface::initializeTechniqueMacroNames()
{
	_techniqueMacroNames.clear();

	for (auto& technique : _effect->techniques())
	{
		auto& techniqueName = technique.first;

		for (auto& pass : technique.second)
			for (auto& macroBinding : pass->macroBindings())
			{
				_techniqueMacroNames[techniqueName].insert(std::get<0>(macroBinding.second));
			}
	}
}

void
Surface::geometry(std::shared_ptr<geometry::Geometry> newGeometry)
{
	for (unsigned int i = 0; i < targets().size(); ++i)
	{
		std::shared_ptr<scene::Node> target = targets()[i];

		target->data()->removeProvider(_geometry->data());
		target->data()->addProvider(newGeometry->data());

		//createDrawCalls();
	}

	_geometry = newGeometry;
}

void
Surface::targetAddedHandler(AbstractComponent::Ptr	ctrl,
							scene::Node::Ptr		target)

{
	auto targetData	= target->data();

	_removedSlot = target->removed()->connect(std::bind(
		&Surface::removedHandler,
		shared_from_this(),
		std::placeholders::_1,
		std::placeholders::_2,
		std::placeholders::_3
	));

	targetData->addProvider(_material);
	targetData->addProvider(_geometry->data());
	targetData->addProvider(_effect->data());
}

void
Surface::removedHandler(NodePtr node, NodePtr target, NodePtr ancestor)
{
	deleteAllDrawCalls();
}

void
Surface::watchMacroAdditionOrDeletion(std::shared_ptr<data::Container> rendererData)
{
	_macroAddedOrRemovedSlots.clear();

	if (targets().empty())
		return;

	auto&	target		= targets().front();
	auto	targetData	= target->data();
	auto	rootData	= target->root()->data();

	_macroAddedOrRemovedSlots.push_back(
		targetData->propertyAdded()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::ADDED
		))
	);

	_macroAddedOrRemovedSlots.push_back(
		targetData->propertyRemoved()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::REMOVED
		))
	);

	_macroAddedOrRemovedSlots.push_back(
		rendererData->propertyAdded()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::ADDED
		))
	);

	_macroAddedOrRemovedSlots.push_back(
		rendererData->propertyRemoved()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::REMOVED
		))
	);

	_macroAddedOrRemovedSlots.push_back(
		rootData->propertyAdded()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::ADDED
		))
	);

	_macroAddedOrRemovedSlots.push_back(
		rootData->propertyRemoved()->connect(std::bind(
			&Surface::macroChangedHandler,
			shared_from_this(),
			std::placeholders::_1,
			std::placeholders::_2,
			MacroChange::REMOVED
		))
	);
}

void
Surface::deleteAllDrawCalls()
{
	auto drawCallsMap = _drawCalls;

	for (auto& drawCalls : drawCallsMap)
		deleteDrawCalls(drawCalls.first);

	_macroPropertyNameToDrawCalls.clear();
	_macroChangedSlots.clear();
}

void
Surface::deleteDrawCalls(std::shared_ptr<data::Container> rendererData)
{
	auto& drawCalls = _drawCalls[rendererData];

	while (!drawCalls.empty())
	{
		auto drawCall = drawCalls.front();

		_drawCallRemoved->execute(shared_from_this(), drawCall);
		drawCalls.pop_front();

		_drawCallToPass.erase(drawCall);
		_drawCallToRendererData.erase(drawCall);

		for (auto& drawcallsIt : _macroPropertyNameToDrawCalls)
		{
			auto& macroName			= drawcallsIt.first;
			auto& macroDrawcalls	= drawcallsIt.second;
			auto  drawcallIt		= std::find(macroDrawcalls.begin(), macroDrawcalls.end(), drawCall);
			
			if (drawcallIt != macroDrawcalls.end())
				macroDrawcalls.erase(drawcallIt);
		}
	}

	// erase in a subsequent step the entries corresponding to macro names which do not monitor any drawcall anymore.
	for (std::unordered_map<std::string, DrawCallList>::iterator drawcallsIt = _macroPropertyNameToDrawCalls.begin();
		drawcallsIt != _macroPropertyNameToDrawCalls.end();
		)
		if (drawcallsIt->second.empty())
			drawcallsIt = _macroPropertyNameToDrawCalls.erase(drawcallsIt);
		else
			++drawcallsIt;

	_drawCalls.erase(rendererData);
}

const Surface::DrawCallList&	
Surface::createDrawCalls(std::shared_ptr<data::Container>	rendererData)
{
	if (_drawCalls.count(rendererData) != 0)
		deleteDrawCalls(rendererData);

#ifdef DEBUG_FALLBACK
	assert(_drawCalls.count(rendererData) == 0);
#endif // DEBUG_FALLBACK

	const auto&	passes		= _effect->technique(_technique);
	auto&		drawCalls	= _drawCalls[rendererData];
	bool		doFallback	= false;

	for (const auto& pass : passes)
	{
		auto drawCall		= initializeDrawCall(pass, rendererData);

		if (drawCall)
		{
			drawCalls.push_back(drawCall);
			_drawCallAdded->execute(shared_from_this(), drawCall);
		}
		else
		{
			doFallback		= true;
			if (_drawCalls.count(rendererData) != 0)
				deleteDrawCalls(rendererData);
		}
	}

	if (!doFallback)
		watchMacroAdditionOrDeletion(rendererData);
	else
	{
		drawCalls.clear();
		switchToFallbackTechnique();
	}

	return drawCalls;
}

DrawCall::Ptr
Surface::initializeDrawCall(Pass::Ptr		pass, 
							Container::Ptr	rendererData,
							DrawCall::Ptr	drawcall)
{
#ifdef DEBUG
	if (pass == nullptr)
		throw std::invalid_argument("pass");
#endif // DEBUG

	const auto target		= targets()[0];
	const auto targetData	= target->data();
	const auto rootData		= target->root()->data();

	std::list<data::ContainerProperty>	booleanMacros;
	std::list<data::ContainerProperty>	integerMacros;
	std::list<data::ContainerProperty>	incorrectIntegerMacros;

	auto program = getWorkingProgram(
		pass, 
		targetData, 
		rendererData, 
		rootData, 
		booleanMacros, 
		integerMacros,
		incorrectIntegerMacros
	);

#ifdef DEBUG_FALLBACK
	assert(incorrectIntegerMacros.empty() != (program==nullptr));
#endif // DEBUG_FALLBACK

	forgiveMacros	(booleanMacros, integerMacros,	TechniquePass(_technique, pass));
	blameMacros		(incorrectIntegerMacros,		TechniquePass(_technique, pass));

	if (!program)
		return nullptr;
	
	if (drawcall == nullptr)
	{
		drawcall = DrawCall::create(
			pass->attributeBindings(),
			pass->uniformBindings(),
			pass->stateBindings(),
			pass->states()
		);

		_drawCallToPass[drawcall]			= pass;
		_drawCallToRendererData[drawcall]	= rendererData;

		for (const auto& binding : pass->macroBindings())
		{
			data::ContainerProperty macro(binding.second, targetData, rendererData, rootData);

			_macroPropertyNameToDrawCalls[macro.name()].push_back(drawcall);

			if (macro.container())
			{
				auto&		listeners		= _numMacroListeners;
				const int	numListeners	= listeners.count(macro) == 0 ? 0 : listeners[macro];

				if (numListeners == 0)
					macroChangedHandler(macro.container(), macro.name(), MacroChange::ADDED);
			}
		}
	}

	drawcall->configure(program, targetData, rendererData, rootData);

	return drawcall;
}

std::shared_ptr<Program>
Surface::getWorkingProgram(std::shared_ptr<Pass>				pass,
						   data::Container::Ptr					targetData,
						   data::Container::Ptr					rendererData,
						   data::Container::Ptr					rootData,
						   std::list<data::ContainerProperty>&	booleanMacros,
						   std::list<data::ContainerProperty>&	integerMacros,
						   std::list<data::ContainerProperty>&	incorrectIntegerMacros)
{
	Program::Ptr program = nullptr;

	do
	{
		program = pass->selectProgram(
			targetData, 
			rendererData, 
			rootData, 
			booleanMacros, 
			integerMacros, 
			incorrectIntegerMacros
		);

		if (program)
			break;
		else
		{
#ifdef DEBUG_FALLBACK
			std::cout << "\t- fallback pass '" << pass->name() << "'\t-> '" << pass->fallback() << "'" << std::endl;
#endif // DEBUG_FALLBACK

			const std::vector<Pass::Ptr>& passes = _effect->technique(_technique);
			auto fallbackIt = std::find_if(passes.begin(), passes.end(), [&](const Pass::Ptr& p)
			{
				return p->name() == pass->fallback();
			});

			if (fallbackIt == passes.end())
				break;
			else
				pass = *fallbackIt;
		}
	}
	while(true);

	return program;

	/***
	auto program = pass->selectProgram(targetData, rendererData, rootData, bindingDefines, bindingValues);

	while (!program)
	{
		auto passes = _effect->technique(_technique);
		auto fallbackIt = std::find_if(
			passes.begin(),
			passes.end(),
			[&](const Pass::Ptr& p)
			{
				return p->name() == pass->fallback();
			}
		);

		if (fallbackIt == passes.end())
			return nullptr;

		pass = *fallbackIt;
		program = pass->selectProgram(targetData, rootData, rendererData, bindingDefines, bindingValues);
	}

	return program;
	***/
}

void
Surface::macroChangedHandler(Container::Ptr		container,
							 const std::string&	propertyName,
							 MacroChange		change)
{
#ifdef DEBUG_FALLBACK
	assert(container);
#endif // DEBUG_FALLBACK

	const data::ContainerProperty	macro		(propertyName, container);
	if (change == MacroChange::REF_CHANGED && !_drawCalls.empty())
	{
		const auto	drawCalls		= _macroPropertyNameToDrawCalls[macro.name()];

		for (auto& drawCall : drawCalls)
		{
			auto	pass			= _drawCallToPass[drawCall];
			auto	rendererData	= _drawCallToRendererData[drawCall];
	
			if (!initializeDrawCall(pass, rendererData, drawCall))
			{
				if (_drawCalls.count(rendererData) != 0)
					deleteDrawCalls(rendererData);

				switchToFallbackTechnique();
				break;
			}
		}
	}
	else if (_techniqueMacroNames.count(_technique) != 0 
		&&   _techniqueMacroNames[_technique].find(macro.name()) != _techniqueMacroNames[_technique].end())
	{
		int numListeners = _numMacroListeners.count(macro) == 0 ? 0 : _numMacroListeners[macro];

		if (change == MacroChange::ADDED)
		{
			if (numListeners == 0)
				_macroChangedSlots[macro] = macro.container()->propertyReferenceChanged(macro.name())->connect(std::bind(
					&Surface::macroChangedHandler,
					shared_from_this(),
					macro.container(),
					macro.name(),
					MacroChange::REF_CHANGED
				));

			_numMacroListeners[macro] = numListeners + 1;
		}
		else if (change == MacroChange::REMOVED)
		{
			macroChangedHandler(macro.container(), macro.name(), MacroChange::REF_CHANGED);

			_numMacroListeners[macro] = numListeners - 1;

			if (_numMacroListeners[macro] == 0)
				_macroChangedSlots.erase(macro);
		}
	}
}

void
Surface::targetRemovedHandler(AbstractComponent::Ptr	ctrl,
							  scene::Node::Ptr			target)
{
	auto data = target->data();

	_removedSlot	= nullptr;
	_macroAddedOrRemovedSlots.clear();

	data->removeProvider(_material);
	data->removeProvider(_geometry->data());
	data->removeProvider(_geometry->data());

	deleteAllDrawCalls();
}

void
Surface::switchToFallbackTechnique()
{
	if (_effect->hasFallback(_technique))
		setTechnique(_effect->fallback(_technique));
}

void
Surface::setTechnique(const std::string& technique)
{
	if (_technique == technique)
		return;

#ifdef DEBUG_FALLBACK
	std::cout << "surf[" << this << "]\tchange technique\t'" << _technique << "'\t-> '" << technique << "'" << std::endl;
#endif	

	_technique = technique;

	if (!_effect->hasTechnique(_technique))
		throw std::logic_error("The technique '" + _technique + "' does not exist.");

	//_macroAddedOrRemovedSlots.clear();
	_techniqueChanged->execute(shared_from_this(), _technique);
}

void
Surface::badMacroChangedHandler(const data::ContainerProperty& macro)
{
	if (_incorrectMacroToPasses.count(macro) > 0)
	{
		std::cout << "bad macro '" << macro.name() << "' changed -> try to switch to '" << _incorrectMacroToPasses[macro].front().first << "'" << std::endl;
		setTechnique(_incorrectMacroToPasses[macro].front().first);
	}
}

void
Surface::blameMacros(const std::list<data::ContainerProperty>& incorrectIntegerMacros,
					 const TechniquePass& pass)
{
	for (auto& macro : incorrectIntegerMacros)
	{
		auto&	failedPasses = _incorrectMacroToPasses[macro];
		auto	failedPassIt = std::find(failedPasses.begin(), failedPasses.end(), pass);
	
		if (failedPassIt == failedPasses.end())
		{
			failedPasses.push_back(pass);
	
#ifdef DEBUG_FALLBACK
			for (auto& techniqueName : _incorrectMacroToPasses[macro])
				std::cout << "'" << macro.name() << "' made [technique '" << pass.first << "' | pass '" << pass.second.get() << "'] fail" << std::endl;
#endif // DEBUG_FALLBACK
		}
	
		if (_incorrectMacroChangedSlot.count(macro) == 0)
		{
			_incorrectMacroChangedSlot[macro] = macro.container()->propertyReferenceChanged(macro.name())->connect(std::bind(
				&Surface::badMacroChangedHandler,
				shared_from_this(),
				macro
			));
		}
	}
}

void
Surface::forgiveMacros(const std::list<data::ContainerProperty>&,
					   const std::list<data::ContainerProperty>& integerMacros,
					   const TechniquePass& pass)
{
	for (auto& macro : integerMacros)
		if (_incorrectMacroToPasses.count(macro) > 0)
		{
			auto&	failedPasses	= _incorrectMacroToPasses[macro];
			auto	failedPassIt	= std::find(failedPasses.begin(), failedPasses.end(), pass);

			if (failedPassIt != failedPasses.end())
			{
				failedPasses.erase(failedPassIt);

				if (failedPasses.empty() 
					&& _incorrectMacroChangedSlot.count(macro) > 0)
					_incorrectMacroChangedSlot.erase(macro);
			}
		}
}