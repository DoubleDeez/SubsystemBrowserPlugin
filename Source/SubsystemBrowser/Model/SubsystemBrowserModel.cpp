// Copyright 2022, Aquanox.

#include "Model/SubsystemBrowserModel.h"
#include "SubsystemBrowserModule.h"
#include "SubsystemBrowserSettings.h"

#define LOCTEXT_NAMESPACE "SubsystemBrowser"

SubsystemCategoryFilter::SubsystemCategoryFilter()
{
	// load initial state from config
	USubsystemBrowserSettings::Get()->LoadCategoryStates(FilterState);
}

bool SubsystemCategoryFilter::PassesFilter(const ISubsystemTreeItem& InItem) const
{
	return IsCategoryVisible(InItem.GetID());
}

void SubsystemCategoryFilter::ShowCategory(FSubsystemTreeItemID InCategory)
{
	FilterState.Add(InCategory, true);
	USubsystemBrowserSettings::Get()->SetCategoryState(InCategory, true);
	OnChangedInternal.Broadcast();
}

void SubsystemCategoryFilter::HideCategory(FSubsystemTreeItemID InCategory)
{
	FilterState.Add(InCategory, false);
	USubsystemBrowserSettings::Get()->SetCategoryState(InCategory, false);
	OnChangedInternal.Broadcast();
}

bool SubsystemCategoryFilter::IsCategoryVisible(FSubsystemTreeItemID InCategory) const
{
	return !FilterState.Contains(InCategory) ? true : FilterState.FindChecked(InCategory);
}

FSubsystemModel::FSubsystemModel()
{
}

TWeakObjectPtr<UWorld> FSubsystemModel::GetCurrentWorld() const
{
	return CurrentWorld;
}

void FSubsystemModel::SetCurrentWorld(TWeakObjectPtr<UWorld> InWorld)
{
	CurrentWorld = InWorld;

	EmptyModel();

	PopulateCategories();
	PopulateSubsystems();
}

const TArray<SubsystemTreeItemPtr>& FSubsystemModel::GetAllCategories() const
{
	return AllCategories;
}

void FSubsystemModel::GetFilteredCategories(TArray<SubsystemTreeItemPtr>& OutCategories) const
{
	OutCategories.Empty();

	for (const SubsystemTreeItemPtr& Item : GetAllCategories())
	{
		check(Item->GetAsCategoryDescriptor());
		if (!CategoryFilter.IsValid() || CategoryFilter->PassesFilter(*Item))
		{
			OutCategories.Add(Item);
		}
	}
}

const TArray<SubsystemTreeItemPtr>& FSubsystemModel::GetAllSubsystems() const
{
	return AllSubsystems;
}

void FSubsystemModel::GetAllSubsystemsInCategory(SubsystemTreeItemPtr Category, TArray<SubsystemTreeItemPtr>& OutChildren) const
{
	check(Category->GetAsCategoryDescriptor());
	OutChildren.Empty();

	if (AllSubsystemsByCategory.Contains(Category->GetID()))
	{
		for (const SubsystemTreeItemPtr& Item : AllSubsystemsByCategory.FindChecked(Category->GetID()))
		{
			OutChildren.Add(Item);
		}
	}
}

void FSubsystemModel::GetFilteredSubsystems(SubsystemTreeItemPtr Category, TArray<SubsystemTreeItemPtr>& OutChildren) const
{
	FSubsystemTreeCategoryItem* AsCategory = Category->GetAsCategoryDescriptor();
	check(AsCategory);

	OutChildren.Empty();

	const USubsystemBrowserSettings* Settings = USubsystemBrowserSettings::Get();

	if (AllSubsystemsByCategory.Contains(AsCategory->GetID()))
	{
		for (const SubsystemTreeItemPtr& Item : AllSubsystemsByCategory.FindChecked(AsCategory->GetID()))
		{
			if (Settings->ShouldShowOnlyGame() && !Item->IsGameModule())
				continue;

			if (!SubsystemTextFilter.IsValid() || SubsystemTextFilter->PassesFilter(*Item))
			{
				OutChildren.Add(Item);
			}
		}
	}

	if (OutChildren.Num() > 1)
	{
		OutChildren.Sort([](SubsystemTreeItemPtr Item1, SubsystemTreeItemPtr Item2) {
			return Item1->GetDisplayNameString() < Item2->GetDisplayNameString();
		});
	}
}

int32 FSubsystemModel::GetNumSubsystemsFromVisibleCategories() const
{
	int32 Count = 0;

	TArray<SubsystemTreeItemPtr> VisibleCategories;
	GetFilteredCategories(VisibleCategories);

	TArray<SubsystemTreeItemPtr> Subsystems;

	for (const auto & Category : VisibleCategories)
	{
		GetAllSubsystemsInCategory(Category, Subsystems);

		Count += Subsystems.Num();
	}

	return Count;
}

TArray<SubsystemColumnPtr> FSubsystemModel::GetDynamicColumns(bool bActiveOnly) const
{
	TArray<SubsystemColumnPtr> Result;

	const USubsystemBrowserSettings* Settings = USubsystemBrowserSettings::Get();
	for (const auto& Column : FSubsystemBrowserModule::Get().GetDynamicColumns())
	{
		if (!bActiveOnly || Settings->GetTableColumnState(Column->Name))
		{
			Result.Add(Column);
		}
	}

	Result.StableSort(SubsystemColumnSorter());

	return Result;
}

SubsystemColumnPtr FSubsystemModel::FindDynamicColumn(const FName& ColumnName, bool bActiveOnly) const
{
	const USubsystemBrowserSettings* Settings = USubsystemBrowserSettings::Get();
	for (const auto& Column : FSubsystemBrowserModule::Get().GetDynamicColumns())
	{
		if (Column->Name == ColumnName && (!bActiveOnly || Settings->GetTableColumnState(Column->Name)))
		{
			return Column;
		}
	}
	return nullptr;
}

void FSubsystemModel::EmptyModel()
{
	for (const auto & Category : AllCategories)
	{
		Category->RemoveAllChildren();
	}
	AllCategories.Empty();

	AllSubsystems.Empty();
	AllSubsystemsByCategory.Empty();
}

void FSubsystemModel::PopulateCategories()
{
	FSubsystemBrowserModule& BrowserModule = FSubsystemBrowserModule::Get();
	for (auto& SubsystemCategory : BrowserModule.GetCategories())
	{
		auto Category = MakeShared<FSubsystemTreeCategoryItem>(SubsystemCategory.ToSharedRef());
		Category->Model = AsShared();
		AllCategories.Add(MoveTemp(Category));
	}

	// sort categories after populating them
	AllCategories.StableSort(SubsystemCategorySorter());
}

void FSubsystemModel::PopulateSubsystems()
{
	check(!AllSubsystems.Num());
	check(!AllSubsystemsByCategory.Num());

	UWorld* const LocalWorld = CurrentWorld.Get();

	for (const auto & Category : AllCategories)
	{
		const FSubsystemTreeCategoryItem* AsCategory = Category->GetAsCategoryDescriptor();

		for (UObject* Impl : AsCategory->Select(LocalWorld))
		{
			auto Descriptor = MakeShared<FSubsystemTreeSubsystemItem>(Impl);
			Descriptor->Model = AsShared();
			Descriptor->Parent = Category;

			AllSubsystems.Add(Descriptor);
			AllSubsystemsByCategory.FindOrAdd(AsCategory->GetID()).Add(Descriptor);
		}
	}
}


#undef LOCTEXT_NAMESPACE
