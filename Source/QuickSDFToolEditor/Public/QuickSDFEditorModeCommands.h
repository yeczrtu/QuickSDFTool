#pragma once
#include "Tools/InteractiveToolsCommands.h"
#include "QuickSDFToolStyle.h"

class FQuickSDFEditorModeCommands : public TCommands<FQuickSDFEditorModeCommands>
{
public:
	FQuickSDFEditorModeCommands() : TCommands<FQuickSDFEditorModeCommands>(
		"QuickSDF",
		NSLOCTEXT("QuickSDF", "QuickSDFCommands", "Quick SDF Mode"),
		NAME_None,
		FQuickSDFToolStyle::GetStyleSetName())
	{}

	virtual void RegisterCommands() override;
	
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands()
	{
		return FQuickSDFEditorModeCommands::Get().Commands;
	}
	
	// ツール切り替え用（ToggleButtonとして動作させる）
	TSharedPtr<FUICommandInfo> SelectTextureAsset;
	TSharedPtr<FUICommandInfo> PaintTextureColor;

	// アクション用（ボタン）
	TSharedPtr<FUICommandInfo> GenerateSDF;
	
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};
