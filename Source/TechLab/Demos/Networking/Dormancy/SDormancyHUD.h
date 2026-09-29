#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UDormancyClientComponent;
class UWorld;
class ATechLabPlayerController;
struct FSlateDynamicImageBrush;

/** Presentation and local input mode only; server requests go through the owned component. */
class SDormancyHUD : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SDormancyHUD) {}
	SLATE_ARGUMENT(UDormancyClientComponent*, Client)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args);

	void TogglePanel();
	void ClosePanel();
	void ResetForWorld();
	bool IsExpanded() const;

private:
	void Rebuild();

	UDormancyClientComponent& GetClient() const;
	ATechLabPlayerController* GetController() const;
	UWorld* GetWorld() const;

	TWeakObjectPtr<UDormancyClientComponent> Client;

	bool bLabExpanded = false;
	bool bLabResults = false;

	// Recorded results and image viewer.
	FString LabResults;
	int32 ResultImageIndex = 0;
	float ResultZoom = .5f;
	TArray<FString> ResultImages;
	TSharedPtr<FSlateDynamicImageBrush> ResultBrush;
};
