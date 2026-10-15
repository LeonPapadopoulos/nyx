#include "NyxPCH.h"
#include "RendererLiveEdits.h"

#include "Log.h"
#include "MaterialAsset.h"
#include "Renderer.h"

#include <string>

namespace Nyx
{
	ELiveEditResult ApplyRendererEdit(const Net::Message& message, Nyx::IRenderer& renderer)
	{
		switch (static_cast<EEditorLinkMessage>(message.Type))
		{
		case EEditorLinkMessage::ShaderProgram:
		{
			ShaderProgramMessage program;
			if (!ReadNetMessage(message, program))
			{
				LOG_ERROR("Live materials: a ShaderProgram message can't be read");
				return ELiveEditResult::Unreadable;
			}
			renderer.ApplyCompiledShader(program.Request, program.Result);
			return ELiveEditResult::Applied;
		}
		case EEditorLinkMessage::MaterialValues:
		{
			MaterialValuesMessage values;
			if (!ReadNetMessage(message, values))
			{
				LOG_ERROR("Live materials: a MaterialValues message can't be read");
				return ELiveEditResult::Unreadable;
			}
			MaterialAsset asset;
			std::string error;
			if (!asset.Parse(values.Text, error))
			{
				LOG_WARNING("Live materials: the values of {} don't parse ({}); the game keeps its own", values.Path, error);
				return ELiveEditResult::Ignored;
			}
			renderer.SetMaterialValues(values.Path, asset);
			return ELiveEditResult::Applied;
		}
		default:
			return ELiveEditResult::NotALiveEdit;
		}
	}
}
