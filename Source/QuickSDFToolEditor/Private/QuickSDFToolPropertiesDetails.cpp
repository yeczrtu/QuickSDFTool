#include "QuickSDFToolPropertiesDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "QuickSDFToolProperties.h"
TSharedRef<IDetailCustomization> FQuickSDFToolPropertiesDetails::MakeInstance()
{
	return MakeShareable(new FQuickSDFToolPropertiesDetails);
}
void FQuickSDFToolPropertiesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Hide raw properties that are now managed by the Timeline UI
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, EditAngleIndex));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, NumAngles));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetAngles));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UQuickSDFToolProperties, TargetTextures));
}
