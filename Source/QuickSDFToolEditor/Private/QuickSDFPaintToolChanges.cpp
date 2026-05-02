#include "QuickSDFPaintToolChanges.h"

#include "QuickSDFPaintTool.h"
#include "QuickSDFPaintToolMaskUtils.h"

namespace QuickSDFPaintToolPrivate
{
void FQuickSDFRenderTargetChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, AfterPixels);
			}
		}
	}
}

void FQuickSDFRenderTargetChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixels))
		{
			if (!AngleGuid.IsValid())
			{
				Tool->ApplyRenderTargetPixels(AngleIndex, BeforePixels);
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < AfterPixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (PixelRects.IsValidIndex(Index) && PixelRects[Index].Width() > 0 && PixelRects[Index].Height() > 0)
			{
				Tool->ApplyRenderTargetPixelsInRectByGuid(AngleGuid, AngleIndices[Index], PixelRects[Index], AfterPixelsByAngle[Index]);
				continue;
			}
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, AfterPixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], AfterPixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFRenderTargetsChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		for (int32 Index = 0; Index < AngleIndices.Num() && Index < BeforePixelsByAngle.Num(); ++Index)
		{
			const FGuid AngleGuid = AngleGuids.IsValidIndex(Index) ? AngleGuids[Index] : FGuid();
			if (PixelRects.IsValidIndex(Index) && PixelRects[Index].Width() > 0 && PixelRects[Index].Height() > 0)
			{
				Tool->ApplyRenderTargetPixelsInRectByGuid(AngleGuid, AngleIndices[Index], PixelRects[Index], BeforePixelsByAngle[Index]);
				continue;
			}
			if (!AngleGuid.IsValid() || !Tool->ApplyRenderTargetPixelsByGuid(AngleGuid, BeforePixelsByAngle[Index]))
			{
				if (!AngleGuid.IsValid())
				{
					Tool->ApplyRenderTargetPixels(AngleIndices[Index], BeforePixelsByAngle[Index]);
				}
			}
		}
	}
}

void FQuickSDFTextureSlotChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, AfterTexture, bAfterAllowSourceTextureOverwrite, AfterPixels);
	}
}

void FQuickSDFTextureSlotChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		Tool->ApplyTextureSlotChange(AngleGuid, AngleIndex, BeforeTexture, bBeforeAllowSourceTextureOverwrite, BeforePixels);
	}
}

void FQuickSDFMaskStateChange::Apply(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, AfterGuids, AfterAngles, AfterTextures, AfterAllowSourceTextureOverwrites, AfterPixelsByMask);
	}
}

void FQuickSDFMaskStateChange::Revert(UObject* Object)
{
	if (UQuickSDFPaintTool* Tool = Cast<UQuickSDFPaintTool>(Object))
	{
		RestoreMaskStateOnNextTick(Tool, BeforeGuids, BeforeAngles, BeforeTextures, BeforeAllowSourceTextureOverwrites, BeforePixelsByMask);
	}
}
}

