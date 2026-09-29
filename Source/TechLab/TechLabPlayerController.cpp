// Copyright Epic Games, Inc. All Rights Reserved.

#include "TechLabPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "TechLab.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Demos/Networking/Dormancy/DormancyClientComponent.h"

void ATechLabPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);
		}
		else
		{

			UE_LOG(LogTechLab, Error, TEXT("Could not spawn mobile controls widget."));
		}
	}
}

void ATechLabPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::F, IE_Pressed, DormancyClient.Get(), &UDormancyClientComponent::RequestAttack);
	InputComponent->BindKey(EKeys::Slash, IE_Pressed, DormancyClient.Get(), &UDormancyClientComponent::TogglePanel);
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, DormancyClient.Get(), &UDormancyClientComponent::ClosePanel);

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
}

bool ATechLabPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

ATechLabPlayerController::ATechLabPlayerController()
{
	DormancyClient = CreateDefaultSubobject<UDormancyClientComponent>(TEXT("DormancyClient"));
}

void ATechLabPlayerController::NotifyLoadedWorld(FName WorldPackageName, bool bFinalDest)
{
	Super::NotifyLoadedWorld(WorldPackageName, bFinalDest);
	if (bFinalDest)
	{
		DormancyClient->OnWorldLoaded();
	}
}
