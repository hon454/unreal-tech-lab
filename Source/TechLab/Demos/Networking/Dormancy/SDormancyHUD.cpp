#include "SDormancyHUD.h"
#include "DormancyClientComponent.h"
#include "TechLabPlayerController.h"
#include "DormancyLab.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SCanvas.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

namespace
{
const FLinearColor TerminalGreen(.18f, 1.f, .42f, 1.f);
} // namespace

void SDormancyHUD::Construct(const FArguments& Args)
{
	Client = Args._Client;
	Rebuild();
}

UDormancyClientComponent& SDormancyHUD::GetClient() const
{
	check(Client.IsValid());
	return *Client.Get();
}

ATechLabPlayerController* SDormancyHUD::GetController() const
{
	return GetClient().GetController();
}

UWorld* SDormancyHUD::GetWorld() const
{
	return GetClient().GetWorld();
}

void SDormancyHUD::TogglePanel()
{
	if (!ADormancyLab::Find(GetWorld()))
	{
		return;
	}

	bLabExpanded = !bLabExpanded;
	GetController()->bShowMouseCursor = bLabExpanded;
	GetController()->ResetIgnoreMoveInput();
	GetController()->ResetIgnoreLookInput();
	if (bLabExpanded)
	{
		if (ADormancyLab* Lab = ADormancyLab::Find(GetWorld()))
		{
			GetClient().GetDraftSettings() = Lab->GetSettings();
		}

		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		GetController()->SetInputMode(Mode);
		GetController()->SetIgnoreMoveInput(true);
		GetController()->SetIgnoreLookInput(true);
	}
	else
	{
		GetController()->SetInputMode(FInputModeGameOnly());
		if (ADormancyLab* Lab = ADormancyLab::Find(GetWorld()); Lab && Lab->IsRunning())
		{
			GetController()->SetIgnoreMoveInput(true);
			GetController()->SetIgnoreLookInput(true);
		}
	}

	Rebuild();
}

void SDormancyHUD::ClosePanel()
{
	if (bLabExpanded)
	{
		ResetForWorld();
	}
}

void SDormancyHUD::ResetForWorld()
{
	bLabExpanded = false;
	GetController()->bShowMouseCursor = false;
	GetController()->ResetIgnoreMoveInput();
	GetController()->ResetIgnoreLookInput();
	GetController()->SetInputMode(FInputModeGameOnly());
	if (ADormancyLab* Lab = ADormancyLab::Find(GetWorld()); Lab && Lab->IsRunning())
	{
		GetController()->SetIgnoreMoveInput(true);
		GetController()->SetIgnoreLookInput(true);
	}

	Rebuild();
}

void SDormancyHUD::Rebuild()
{
	if (!GetController()->IsLocalController() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	if (!ADormancyLab::Find(GetWorld()))
	{
		return;
	}

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	auto Button = [](const FString& Text, TFunction<void()> Fn) -> TSharedRef<SButton> {
		return SNew(SButton)
			.ForegroundColor(TerminalGreen)
			.IsFocusable(false)
			.ContentPadding(FMargin(10, 5))
			.Text(FText::FromString(Text))
			.OnClicked_Lambda([Fn]() {
				Fn();
				return FReply::Handled();
			});
	};

	Column->AddSlot().AutoHeight()[Button(
		bLabExpanded ? TEXT("RESOURCE DORMANCY | Collapse [/ or Esc]") : TEXT("F: attack | [/]: experiment / results"), [this] {
			TogglePanel();
		})];

	Column->AddSlot().AutoHeight().Padding(0, 4)
		[SNew(STextBlock)
			 .ColorAndOpacity(TerminalGreen)
			 .ShadowColorAndOpacity(FLinearColor::Black)
			 .ShadowOffset(FVector2D(1, 1))
			 .Text_Lambda([this] {
				 const ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
				 if (!Lab)
				 {
					 return FText::GetEmpty();
				 }

				 return FText::FromString(FString::Printf(
					 TEXT("%s | %s | field nodes %d | Awake %d | changes %d | flush %d | wake %d\nRun %s\n%s\nPolicy %d | configured nodes "
						  "%d | %d%% every %.2fs"),
					 GetWorld()->GetNetMode() == NM_ListenServer ? TEXT("SERVER VIEW") : TEXT("CLIENT VIEW"), *Lab->GetPhase(),
					 Lab->GetRun().IsValid() ? Lab->GetSettings().Count : 0, Lab->GetAwake(), Lab->GetChanges(), Lab->GetFlushes(),
					 Lab->GetWakes(), *Lab->GetRun().ToString(), *Lab->GetConnectionStats(), Lab->GetSettings().Scenario,
					 Lab->GetSettings().Count, Lab->GetSettings().Percent, Lab->GetSettings().Interval));
			 })
			 .AutoWrapText(true)];

	if (bLabExpanded)
	{
		Column->AddSlot().AutoHeight().Padding(0, 6)[SNew(STextBlock)
														 .ColorAndOpacity(TerminalGreen)
														 .ShadowColorAndOpacity(FLinearColor::Black)
														 .ShadowOffset(FVector2D(1, 1))
														 .Text_Lambda([this] {
															 return FText::FromString(GetClient().GetLiveText());
														 })
														 .AutoWrapText(true)];
		Column->AddSlot().AutoHeight()[SNew(SBox).IsEnabled_Lambda([this] {
			ADormancyLab* L = ADormancyLab::Find(GetWorld());
			return L && L->CanControl(GetController()) && L->IsRunning();
		})[Button(TEXT("STOP (then verify final state)"), [this] {
			GetClient().RequestLab(1);
		})]];
		TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
		Tabs->AddSlot().AutoWidth()[Button(TEXT("EXHIBIT / LIVE STATUS"), [this] {
			bLabResults = false;
			Rebuild();
		})];

		Tabs->AddSlot().AutoWidth()[Button(TEXT("RESULTS / EVIDENCE"), [this] {
			bLabResults = true;
			LabResults = TEXT("미측정 / NOT MEASURED\nNo published trace analysis yet. Live counters are self-instrumentation, not engine "
							  "CPU or packet measurements.");
			FString LoadedResults;
			if (FFileHelper::LoadFileToString(LoadedResults, *(FPaths::ProjectContentDir() / TEXT("DormancyResults/resource-summary.txt"))))
			{
				LabResults = MoveTemp(LoadedResults);
			}

			ResultImages.Empty();
			IFileManager::Get().FindFiles(ResultImages, *(FPaths::ProjectContentDir() / TEXT("DormancyResults/resource-*.png")), true,
										  false);
			ResultImages.Sort();
			ResultImageIndex = 0;
			Rebuild();
		})];

		Column->AddSlot().AutoHeight()[Tabs];
		if (!bLabResults)
		{
			TSharedRef<SVerticalBox> Controls = SNew(SVerticalBox).IsEnabled_Lambda([this] {
				ADormancyLab* L = ADormancyLab::Find(GetWorld());
				return L && L->CanControl(GetController()) && !L->IsRunning();
			});

			Controls->AddSlot().AutoHeight()[Button(TEXT("Restore selected exhibit pair"), [this] {
				GetClient().RestoreSelectedPair();
			})];
			Controls->AddSlot()
				.AutoHeight()[SNew(STextBlock)
								  .ColorAndOpacity(TerminalGreen)
								  .ShadowColorAndOpacity(FLinearColor::Black)
								  .ShadowOffset(FVector2D(1, 1))
								  .Text(FText::FromString(TEXT("Draft settings: APPLY shares them; START / COMPARE also apply them.")))
								  .AutoWrapText(true)];
			Controls->AddSlot().AutoHeight()[Button(
				FString::Printf(TEXT("Policy: %s"), GetClient().GetDraftSettings().Scenario == 0   ? TEXT("Always Awake")
													: GetClient().GetDraftSettings().Scenario == 1 ? TEXT("Flush before damage")
																								   : TEXT("Awake during damage")),
				[this] {
					GetClient().GetDraftSettings().Scenario = (GetClient().GetDraftSettings().Scenario + 1) % 3;
					Rebuild();
				})];
			Controls->AddSlot().AutoHeight()[Button(FString::Printf(TEXT("Resources: %d"), GetClient().GetDraftSettings().Count), [this] {
				GetClient().GetDraftSettings().Count =
					GetClient().GetDraftSettings().Count >= 8192 ? 256 : GetClient().GetDraftSettings().Count * 2;
				Rebuild();
			})];
			Controls->AddSlot().AutoHeight()[Button(FString::Printf(TEXT("Damaged: %d%%"), GetClient().GetDraftSettings().Percent), [this] {
				int32& Percent = GetClient().GetDraftSettings().Percent;
				Percent = Percent == 0 ? 1 : Percent == 1 ? 10 : Percent == 10 ? 50 : Percent == 50 ? 100 : 0;
				Rebuild();
			})];
			Controls->AddSlot()
				.AutoHeight()[Button(FString::Printf(TEXT("Interval: %.2f seconds"), GetClient().GetDraftSettings().Interval), [this] {
					float& Interval = GetClient().GetDraftSettings().Interval;
					Interval = Interval > .51f ? .5f : Interval > .11f ? .1f : Interval > .051f ? .05f : 1.f;
					Rebuild();
				})];
			Controls->AddSlot().AutoHeight()[Button(TEXT("APPLY SETTINGS"), [this] {
				GetClient().RequestLab(5);
			})];
			Controls->AddSlot().AutoHeight()[Button(TEXT("START SELECTED POLICY"), [this] {
				GetClient().RequestLab(0);
			})];
			Controls->AddSlot().AutoHeight()[Button(TEXT("COMPARE ALL THREE POLICIES"), [this] {
				GetClient().RequestLab(6);
			})];
			Controls->AddSlot().AutoHeight()[Button(TEXT("CLEAR FIELD"), [this] {
				GetClient().RequestLab(2);
			})];
			Controls->AddSlot().AutoHeight()[Button(TEXT("Restart exhibit / Initial (reload for ALL)"), [this] {
				GetClient().RequestLab(7);
			})];

			Column->AddSlot().AutoHeight().Padding(0, 8)[Controls];

			Column->AddSlot()
				.AutoHeight()[SNew(STextBlock)
								  .ColorAndOpacity(TerminalGreen)
								  .ShadowColorAndOpacity(FLinearColor::Black)
								  .ShadowOffset(FVector2D(1, 1))
								  .AutoWrapText(true)
								  .Text(FText::FromString(TEXT(
									  "F: attack nearby resource pair. 100 HP / 20 damage. Each cube displays its own replicated "
									  "health.\nCompare independent runs with "
									  "identical settings using this HUD.\nWhile running, / > STOP ends the run and "
									  "verifies final state.\nLive counters are custom instrumentation. Dormancy counts are configured "
									  "states, not per-connection completion.\n/ opens/closes. Settings lock while running.")))];
		}
		else
		{
			TSharedRef<SScrollBox> ResultsScroll = SNew(SScrollBox);
			ResultsScroll->AddSlot()[SNew(STextBlock)
										 .ColorAndOpacity(TerminalGreen)
										 .ShadowColorAndOpacity(FLinearColor::Black)
										 .ShadowOffset(FVector2D(1, 1))
										 .Text(FText::FromString(LabResults))
										 .AutoWrapText(true)];
			ResultsScroll->AddSlot()[SNew(STextBlock)
										 .ColorAndOpacity(TerminalGreen)
										 .ShadowColorAndOpacity(FLinearColor::Black)
										 .ShadowOffset(FVector2D(1, 1))
										 .Text_Lambda([this] {
											 ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
											 return FText::FromString(Lab ? Lab->GetSessionResults() : FString());
										 })
										 .AutoWrapText(true)];
			Column->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(230)[ResultsScroll]];
			if (!ResultImages.IsEmpty())
			{
				TSharedRef<SHorizontalBox> Zoom = SNew(SHorizontalBox);
				Zoom->AddSlot().AutoWidth()[Button(TEXT("Next image"), [this] {
					ResultImageIndex = (ResultImageIndex + 1) % ResultImages.Num();
					Rebuild();
				})];

				Zoom->AddSlot().AutoWidth()[Button(TEXT("Zoom +"), [this] {
					ResultZoom = FMath::Min(3.f, ResultZoom + .25f);
					Rebuild();
				})];

				Zoom->AddSlot().AutoWidth()[Button(TEXT("Zoom -"), [this] {
					ResultZoom = FMath::Max(.25f, ResultZoom - .25f);
					Rebuild();
				})];

				Column->AddSlot().AutoHeight()[Zoom];
				const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("DormancyResults") /
																	   ResultImages[ResultImageIndex]);
				const FName Resource(*File);
				const FIntPoint Size = FSlateApplication::Get().GetRenderer()->GenerateDynamicImageResource(Resource);
				ResultBrush = MakeShared<FSlateDynamicImageBrush>(Resource, FVector2D(Size));
				Column->AddSlot().AutoHeight()[SNew(STextBlock)
												   .ColorAndOpacity(TerminalGreen)
												   .ShadowColorAndOpacity(FLinearColor::Black)
												   .ShadowOffset(FVector2D(1, 1))
												   .Text(FText::FromString(ResultImages[ResultImageIndex]))];
				Column->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(
					300)[SNew(SScrollBox).Orientation(Orient_Vertical) +
						 SScrollBox::Slot()[SNew(SScrollBox).Orientation(Orient_Horizontal) +
											SScrollBox::Slot()[SNew(SBox)
																   .WidthOverride(Size.X * ResultZoom)
																   .HeightOverride(Size.Y *
																				   ResultZoom)[SNew(SImage).Image(ResultBrush.Get())]]]]];
			}
		}
	}

	int32 ViewX = 1280, ViewY = 720;
	GetController()->GetViewportSize(ViewX, ViewY);
	ViewX = FMath::Max(320, ViewX);
	ViewY = FMath::Max(240, ViewY);
	const float Scale = FMath::Max(.5f, UWidgetLayoutLibrary::GetViewportScale(GetController()));
	TSharedRef<SCanvas> Health = SNew(SCanvas).Visibility(EVisibility::HitTestInvisible);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Health->AddSlot()
			.Position_Lambda([this, Index] {
				return GetClient().GetResourcePosition(Index);
			})
			.Size(FVector2D(
				240, 52))[SNew(SVerticalBox) +
						  SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
																.ColorAndOpacity(TerminalGreen)
																.ShadowColorAndOpacity(FLinearColor::Black)
																.ShadowOffset(FVector2D(1, 1))
																.Text_Lambda([this, Index] {
																	return FText::FromString(GetClient().GetResourceText(Index));
																})] +
						  SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(10)[SNew(SProgressBar).Percent_Lambda([this, Index] {
							  return GetClient().GetResourceFraction(Index);
						  })]]];
	}

	ChildSlot[SNew(SOverlay) + SOverlay::Slot()[Health] +
			  SOverlay::Slot()[SNew(SVerticalBox) +
							   SVerticalBox::Slot()
								   .AutoHeight()
								   .HAlign(HAlign_Left)
								   .Padding(16)[SNew(SBox)
													.WidthOverride(FMath::Min(bLabExpanded ? 650.f : 340.f, ViewX / Scale - 32.f))
													.MaxDesiredHeight(FMath::Max(200.f, ViewY / Scale - 32.f))
														[SNew(SBorder).Padding(12).BorderBackgroundColor(FLinearColor(
															.015f, .03f, .055f, .96f))[SNew(SScrollBox) + SScrollBox::Slot()[Column]]]]]];
}

bool SDormancyHUD::IsExpanded() const
{
	return bLabExpanded;
}
