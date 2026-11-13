#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructDeveloperSettings.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModScriptsSwitchTest,
                                 "Construct.Shell.Settings.ModScriptsOffByDefault",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModScriptsSwitchTest::RunTest(const FString& Parameters)
{
	const FBoolProperty* Switch = FindFProperty<FBoolProperty>(UConstructDeveloperSettings::StaticClass(), TEXT("bEnableModScripts"));
	if (!TestNotNull(TEXT("The Mod Scripts switch exists"), Switch))
	{
		return false;
	}
	TestTrue(TEXT("The switch is saved in the project's config"), Switch->HasAnyPropertyFlags(CPF_Config));
	TestFalse(TEXT("A fresh project does not opt in to Mod Scripts"), GetDefault<UConstructDeveloperSettings>()->bEnableModScripts);
	return true;
}
#endif
