#include "QuickSDFEditorModeCommands.h"

#include "QuickSDFEditorMode.h"

#define LOCTEXT_NAMESPACE "QuickSDFEditorModeCommands"

void FQuickSDFEditorModeCommands::RegisterCommands()
{
	UI_COMMAND(SelectTextureAsset, "Select", "Select Mesh", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(PaintTextureColor, "Paint", "Paint SDF Mask", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(GenerateSDF, "Generate", "Generate SDF Texture", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(PreviousFrame, "Previous Frame", "Select the previous Quick SDF timeline frame", EUserInterfaceActionType::Button, FInputChord(EKeys::Left));
	UI_COMMAND(NextFrame, "Next Frame", "Select the next Quick SDF timeline frame", EUserInterfaceActionType::Button, FInputChord(EKeys::Right));
	
	TArray<TSharedPtr<FUICommandInfo>> QuickSDFCommands = {
		SelectTextureAsset, PaintTextureColor
	};
	Commands.Add(FName(TEXT("Default")), QuickSDFCommands);
}

#undef LOCTEXT_NAMESPACE
