#include "MainDungeonGenerator.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/EngineTypes.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#include "Engine/OverlapResult.h"

AMainDungeonGenerator::AMainDungeonGenerator()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AMainDungeonGenerator::BeginPlay()
{
    Super::BeginPlay();
    GenerateDungeon();
}

void AMainDungeonGenerator::GenerateDungeon()
{
    if (!FirstRoomBlueprint || CorridorBlueprints.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No first room or corridor blueprints set."));
        return;
    }

    if (RandomSeed == 0)
    {
        RandomSeed = FMath::Rand();
        UE_LOG(LogTemp, Warning, TEXT("RandomSeed not provided; using generated seed: %d"), RandomSeed);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Using provided RandomSeed: %d"), RandomSeed);
    }

    DungeonRNG.Initialize(RandomSeed);

    FVector Location = GetActorLocation();
    FRotator Rotation = FRotator::ZeroRotator;

    AActor* FirstRoom = GetWorld()->SpawnActor<AActor>(FirstRoomBlueprint, Location, Rotation);
    if (!FirstRoom) return;

    SpawnedRooms.Add(FirstRoom);
    RoomsSpawned = 1;

    while (RoomsSpawned < NumRoomsToGenerate)
    {
        if (!GenerateNextRoom())
        {
            UE_LOG(LogTemp, Error, TEXT("Generation stopped early due to failure."));
            break;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Dungeon generation complete. Total rooms: %d"), RoomsSpawned);

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (NavSys)
    {
        NavSys->Build();
        UE_LOG(LogTemp, Warning, TEXT("Navigation mesh rebuilt."));
    }

    for (AActor* Room : SpawnedRooms)
    {
        if (!Room) continue;

        UFunction* SpawnFunction = Room->FindFunction(FName("SpawnEnemies"));
        if (SpawnFunction)
        {
            Room->ProcessEvent(SpawnFunction, nullptr);
            UE_LOG(LogTemp, Warning, TEXT("SpawnEnemies called on: %s"), *Room->GetName());
        }
    }
}

bool AMainDungeonGenerator::GenerateNextRoom()
{
    if (RoomsSpawned >= NumRoomsToGenerate)
    {
        UE_LOG(LogTemp, Warning, TEXT("Dungeon generation complete."));
        return false;
    }

    TArray<UArrowComponent*> AllAvailableExits;
    for (AActor* Room : SpawnedRooms)
    {
        TArray<UArrowComponent*> RoomExits = GetExitArrows(Room);
        for (UArrowComponent* Exit : RoomExits)
        {
            if (!UsedArrows.Contains(Exit))
            {
                AllAvailableExits.Add(Exit);
            }
        }
    }

    if (AllAvailableExits.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("No more available exits."));
        return false;
    }

    while (AllAvailableExits.Num() > 0)
    {
        int32 Index = DungeonRNG.RandRange(0, AllAvailableExits.Num() - 1);
        UArrowComponent* ExitArrow = AllAvailableExits[Index];
        AllAvailableExits.RemoveAt(Index);

        int32 CorridorIndex = DungeonRNG.RandRange(0, CorridorBlueprints.Num() - 1);
        TSubclassOf<AActor> CorridorClass = CorridorBlueprints[CorridorIndex];

        AActor* Corridor = GetWorld()->SpawnActor<AActor>(CorridorClass);
        if (!Corridor) continue;

        TArray<UArrowComponent*> CorridorArrows = GetExitArrows(Corridor);
        if (CorridorArrows.Num() < 2)
        {
            UE_LOG(LogTemp, Error, TEXT("Corridor missing arrows: %s"), *Corridor->GetName());
            Corridor->Destroy();
            continue;
        }

        UArrowComponent* CorridorStart = CorridorArrows[0];
        UArrowComponent* CorridorEnd = CorridorArrows[1];

        AlignActorToArrow(ExitArrow, CorridorStart, Corridor);

        bool bPlaceEndRoom = (RoomsSpawned == NumRoomsToGenerate - 1 && EndRoomBlueprint);
        bool bPlaceShop = (RoomsSpawned == NumRoomsToGenerate - 2 && ShopBlueprint);

        TSubclassOf<AActor> RoomClass;
        if (bPlaceEndRoom)
        {
            UE_LOG(LogTemp, Warning, TEXT("Placing end room."));
            RoomClass = EndRoomBlueprint;
        }
        else if (bPlaceShop)
        {
            UE_LOG(LogTemp, Warning, TEXT("Placing shop room."));
            RoomClass = ShopBlueprint;
        }
        else
        {
            RoomClass = RoomBlueprints[DungeonRNG.RandRange(0, RoomBlueprints.Num() - 1)];
        }

        AActor* NextRoom = GetWorld()->SpawnActor<AActor>(RoomClass);
        if (!NextRoom)
        {
            Corridor->Destroy();
            continue;
        }

        TArray<UArrowComponent*> Entrances = GetExitArrows(NextRoom);
        if (Entrances.Num() == 0)
        {
            Corridor->Destroy();
            NextRoom->Destroy();
            continue;
        }

        UArrowComponent* EntranceArrow = Entrances[DungeonRNG.RandRange(0, Entrances.Num() - 1)];
        AlignActorToArrow(CorridorEnd, EntranceArrow, NextRoom);

        if (CheckOverlap(Corridor) || CheckOverlap(NextRoom))
        {
            Corridor->Destroy();
            NextRoom->Destroy();
            UsedArrows.Add(ExitArrow);
            continue;
        }

        UsedArrows.Add(ExitArrow);
        SpawnedRooms.Add(NextRoom);
        RoomsSpawned++;

        DestroyArrowWithChildren(ExitArrow);
        DestroyArrowWithChildren(CorridorStart);
        DestroyArrowWithChildren(CorridorEnd);
        DestroyArrowWithChildren(EntranceArrow);

        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("Room %d failed to place."), RoomsSpawned);
    return false;
}

void AMainDungeonGenerator::AlignActorToArrow(UArrowComponent* TargetArrow, UArrowComponent* SourceArrow, AActor* ActorToMove)
{
    if (!TargetArrow || !SourceArrow || !ActorToMove) return;

    FVector TargetLocation = TargetArrow->GetComponentLocation();
    FVector TargetForward = TargetArrow->GetForwardVector();
    FVector SourceLocation = SourceArrow->GetComponentLocation();

    FRotator AlignRotation = UKismetMathLibrary::MakeRotFromXZ(-TargetForward, FVector::UpVector);
    FQuat SourceRelativeRot = SourceArrow->GetComponentQuat().Inverse() * ActorToMove->GetActorQuat();
    FQuat FinalRot = AlignRotation.Quaternion() * SourceRelativeRot;

    FVector Offset = SourceLocation - ActorToMove->GetActorLocation();
    FVector AlignedLocation = TargetLocation - FinalRot.RotateVector(Offset);

    ActorToMove->SetActorLocationAndRotation(AlignedLocation, FinalRot.Rotator());
}

TArray<UArrowComponent*> AMainDungeonGenerator::GetExitArrows(AActor* RoomActor, const FName& ExitListName)
{
    TArray<UArrowComponent*> Arrows;
    if (!RoomActor) return Arrows;

    TArray<USceneComponent*> Components;
    RoomActor->GetComponents<USceneComponent>(Components);

    for (USceneComponent* Comp : Components)
    {
        if (Comp->GetName() == ExitListName.ToString())
        {
            TArray<USceneComponent*> LocalChildren;
            Comp->GetChildrenComponents(true, LocalChildren);

            for (USceneComponent* Child : LocalChildren)
            {
                if (UArrowComponent* Arrow = Cast<UArrowComponent>(Child))
                {
                    Arrows.Add(Arrow);
                }
            }
            break;
        }
    }

    return Arrows;
}

bool AMainDungeonGenerator::CheckOverlap(AActor* Actor)
{
    if (!Actor) return true;

    FCollisionQueryParams QueryParams;
    QueryParams.AddIgnoredActor(Actor);

    UBoxComponent* Collider = Actor->FindComponentByClass<UBoxComponent>();
    if (!Collider)
    {
        UE_LOG(LogTemp, Warning, TEXT("No Collider found on: %s"), *Actor->GetName());
        return false;
    }

    FVector Location = Collider->GetComponentLocation();
    FVector Extent = Collider->GetScaledBoxExtent();
    FQuat Rotation = Collider->GetComponentQuat();

    TArray<FOverlapResult> Overlaps;
    GetWorld()->OverlapMultiByChannel(
        Overlaps,
        Location,
        Rotation,
        ECC_WorldDynamic,
        FCollisionShape::MakeBox(Extent),
        QueryParams
    );

    for (const FOverlapResult& Result : Overlaps)
    {
        AActor* OverlapActor = Result.GetActor();
        if (OverlapActor && OverlapActor != Actor)
        {
            UE_LOG(LogTemp, Warning, TEXT("Overlap with: %s"), *OverlapActor->GetName());
            return true;
        }
    }

    return false;
}

void AMainDungeonGenerator::DestroyArrowWithChildren(USceneComponent* Arrow)
{
    if (!Arrow || !Arrow->IsValidLowLevel()) return;

    TArray<USceneComponent*> ChildComponents;
    Arrow->GetChildrenComponents(true, ChildComponents);

    for (USceneComponent* Child : ChildComponents)
    {
        if (Child && Child->IsValidLowLevel())
        {
            Child->DestroyComponent();
        }
    }

    Arrow->DestroyComponent();
}
