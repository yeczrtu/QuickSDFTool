#pragma once
#include "Blueprint/UserWidget.h"
#include "QuickSDFPaintTool.h"
#include "QuickSDFPreviewWidget.generated.h"

UCLASS()
class UQuickSDFPreviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ツールへの参照を保持
	UPROPERTY(BlueprintReadOnly, Category = "SDF")
	UQuickSDFPaintTool* ParentTool;

	// 画像を表示するImageウィジェット（Blueprint側でバインド）
	UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "SDF")
	class UImage* PreviewImage;

	// UV線を描画するための処理
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};