#include "QuickSDFPreviewWidget.h"

#include "InteractiveToolManager.h"
#include "QuickSDFEditorMode.h"
#include "Components/Image.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EditorModeManager.h"

int32 UQuickSDFPreviewWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!ParentTool)
	{
		// EditorModeManagerから現在のアクティブなモードを取得
		// "EM_QuickSDFEditorMode" は Mode.cpp で定義した ID に合わせてください
		UQuickSDFEditorMode* Mode = Cast<UQuickSDFEditorMode>(GLevelEditorModeTools().GetActiveScriptableMode("EM_QuickSDFEditorMode"));
		if (Mode && Mode->GetToolManager())
		{
			// Constキャストが必要な場合があります（NativePaintがconstなため）
			UQuickSDFPreviewWidget* MutableThis = const_cast<UQuickSDFPreviewWidget*>(this);
			MutableThis->ParentTool = Cast<UQuickSDFPaintTool>(Mode->GetToolManager()->GetActiveTool(EToolSide::Left));
		}
	}

	int32 NextLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	
	if (!ParentTool) return NextLayer;

	// 1. レンダーターゲットの取得
	UTextureRenderTarget2D* ActiveRT = ParentTool->GetActiveRenderTarget();
	if (ActiveRT && PreviewImage)
	{
		// RenderTargetをObjectとしてImageにセット
		PreviewImage->SetBrushResourceObject(Cast<UObject>(ActiveRT));
	}

	/*
	if (ParentTool->TargetMesh.IsValid() && ParentTool->Properties)
	{
		const UE::Geometry::FDynamicMeshAttributeSet* Attributes = ParentTool->TargetMesh->Attributes();
		if (Attributes)
		{
			const UE::Geometry::FDynamicMeshUVOverlay* UVOverlay = Attributes->GetUVLayer(ParentTool->Properties->UVChannel);
			if (UVOverlay)
			{
				FLinearColor UVColor(0, 1, 0, 0.1f);
				FVector2D LocalSize = AllottedGeometry.GetLocalSize();

				for (int32 Tid : ParentTool->TargetMesh->TriangleIndicesItr())
				{
					if (UVOverlay->IsSetTriangle(Tid))
					{
						UE::Geometry::FIndex3i UVIndices = UVOverlay->GetTriangle(Tid);
						
						FVector2f UVs[3] = { 
							UVOverlay->GetElement(UVIndices.A), 
							UVOverlay->GetElement(UVIndices.B), 
							UVOverlay->GetElement(UVIndices.C) 
						};

						for (int i = 0; i < 3; ++i)
						{
							TArray<FVector2D> Points;
							Points.Add(FVector2D(UVs[i].X * LocalSize.X, UVs[i].Y * LocalSize.Y));
							Points.Add(FVector2D(UVs[(i + 1) % 3].X * LocalSize.X, UVs[(i + 1) % 3].Y * LocalSize.Y));
							
							FSlateDrawElement::MakeLines(
								OutDrawElements,
								NextLayer + 1,
								AllottedGeometry.ToPaintGeometry(),
								Points,
								ESlateDrawEffect::None,
								UVColor,
								true,
								0.25f
							);
						}
					}
				}
				NextLayer++;
			}
		}
	}
	*/

	return NextLayer;
}
