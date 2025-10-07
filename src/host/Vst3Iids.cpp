#include <pluginterfaces/base/istringresult.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>

namespace Steinberg
{
const FUID IStringResult::iid (INLINE_UID(0x550798BC, 0x872049DB, 0x84920A15, 0x3B50B7A8));
const FUID IString::iid (INLINE_UID(0xF99DB7A3, 0x0FC14821, 0x800B0CF9, 0x8E348EDF));
} // namespace Steinberg

namespace Steinberg::Vst
{
const FUID IComponent::iid (INLINE_UID(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802));
const FUID IAudioProcessor::iid (INLINE_UID(0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D));
const FUID IEditController::iid (INLINE_UID(0xDCD7BBE3, 0x7742448D, 0xA874AACC, 0x979C759E));
const FUID IEventList::iid (INLINE_UID(0x3A2C4214, 0x346349FE, 0xB2C4F397, 0xB9695A44));
const FUID IParamValueQueue::iid (INLINE_UID(0x01263A18, 0xED074F6F, 0x98C9D356, 0x4686F9BA));
const FUID IParameterChanges::iid (INLINE_UID(0xA4779663, 0x0BB64A56, 0xB44384A8, 0x466FEB9D));
} // namespace Steinberg::Vst
