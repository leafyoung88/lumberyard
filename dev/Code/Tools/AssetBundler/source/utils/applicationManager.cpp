/*
 * All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
 * its licensors.
 *
 * For complete copyright and license terms please see the LICENSE at the root of this
 * distribution (the "License"). All use of this software is governed by the License,
 * or, if provided, by the license below or the license accompanying this file. Do not
 * remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 */

#include <source/Utils/applicationManager.h>

#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Asset/AssetManagerComponent.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/Jobs/JobManagerComponent.h>
#include <AzCore/Memory/MemoryComponent.h>
#include <AzCore/Module/DynamicModuleHandle.h>
#include <AzCore/Module/ModuleManagerBus.h>
#include <AzCore/Slice/SliceSystemComponent.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/UserSettings/UserSettingsComponent.h>

#include <AzFramework/Asset/AssetCatalogComponent.h>
#include <AzFramework/Components/BootstrapReaderComponent.h>
#include <AzFramework/Driller/RemoteDrillerInterface.h>
#include <AzFramework/Entity/GameEntityContextComponent.h>
#include <AzFramework/FileTag/FileTagComponent.h>
#include <AzFramework/Input/System/InputSystemComponent.h>
#include <AzFramework/Platform/PlatformDefaults.h>
#include <AzFramework/StringFunc/StringFunc.h>

#include <AzToolsFramework/Archive/ArchiveComponent.h>
#include <AzToolsFramework/Asset/AssetDebugInfo.h>
#include <AzToolsFramework/Asset/AssetUtils.h>
#include <AzToolsFramework/AssetBundle/AssetBundleComponent.h>
#include <AzToolsFramework/AssetCatalog/PlatformAddressedAssetCatalogBus.h>
#include <AzCore/Jobs/Algorithms.h>

namespace AssetBundler
{
    const char GemsDirectoryName[] = "Gems";
    const char compareVariablePrefix = '$';

    GemInfo::GemInfo(AZStd::string name, AZStd::string relativeFilePath, AZStd::string absoluteFilePath)
        : m_gemName(name)
        , m_relativeFilePath(relativeFilePath)
        , m_absoluteFilePath(absoluteFilePath)
    {
    }

    ApplicationManager::ApplicationManager(int* argc, char*** argv)
        : AzToolsFramework::ToolsApplication(argc, argv)
    {
    }

    ApplicationManager::~ApplicationManager()
    {
        DestroyApplication();
    }

    void ApplicationManager::Init()
    {
        AZ::Debug::TraceMessageBus::Handler::BusConnect();
        Start(AzFramework::Application::Descriptor());
        AZ::SerializeContext* context;
        EBUS_EVENT_RESULT(context, AZ::ComponentApplicationBus, GetSerializeContext);
        AZ_Assert(context, "No serialize context");
        AzToolsFramework::AssetSeedManager::Reflect(context);
        AzToolsFramework::AssetFileInfoListComparison::Reflect(context);
        AzToolsFramework::AssetBundleSettings::Reflect(context);

        AZ::IO::FileIOBase* fileIO = AZ::IO::FileIOBase::GetInstance();
        AZ_Assert(fileIO != nullptr, "AZ::IO::FileIOBase must be ready for use.\n");

        m_assetSeedManager = AZStd::make_unique<AzToolsFramework::AssetSeedManager>();

        AZ_TracePrintf(AssetBundler::AppWindowName, "\n");
    }

    void ApplicationManager::DestroyApplication()
    {
        m_showVerboseOutput = false;
        ShutDownMetrics();
        m_assetSeedManager.reset();
        Stop();
        AZ::Debug::TraceMessageBus::Handler::BusDisconnect();
    }

    bool ApplicationManager::Run()
    {
        InitMetrics();

        const AzFramework::CommandLine* parser = GetCommandLine();

        bool shouldPrintHelp = ShouldPrintHelp(parser);

        // Check for what command we are running, and if the user wants to see the Help text
        m_commandType = GetCommandType(parser, shouldPrintHelp);

        if (shouldPrintHelp)
        {
            // If someone requested the help text, it doesn't matter if their command is invalid
            OutputHelp(m_commandType);
            return true;
        }

        if (m_commandType == CommandType::Invalid)
        {
            OutputHelp(m_commandType);
            return false;
        }

        m_showVerboseOutput = ShouldPrintVerbose(parser);

        ComputeEngineRoot();

        AZStd::string platformName(AzToolsFramework::AssetSystem::GetHostAssetPlatform());
        AZStd::string assetsAlias;
        AZStd::string assetCatalogFile;

        AZ::Outcome<void, AZStd::string> result = AssetBundler::ComputeAssetAliasAndGameName(platformName, assetCatalogFile, assetsAlias, m_currentProjectName);
        if (!result.IsSuccess())
        {
            SendErrorMetricEvent(result.GetError().c_str());
            AZ_Error(AppWindowName, false, result.GetError().c_str());
            return false;
        }

        const char* appRoot = nullptr;
        AzFramework::ApplicationRequests::Bus::BroadcastResult(appRoot, &AzFramework::ApplicationRequests::GetAppRoot);

        // Gems
        if (!AzToolsFramework::AssetUtils::GetGemsInfo(g_cachedEngineRoot, appRoot, m_currentProjectName.c_str(), m_gemInfoList))
        {
            SendErrorMetricEvent("Failed to read Gems.");
            AZ_Error(AppWindowName, false, "Failed to read Gems for project: %s\n", m_currentProjectName);
            return false;
        }

        // @assets@ alias
        AZ_TracePrintf(AssetBundler::AppWindowNameVerbose, "Setting asset alias to ( %s ).\n", assetsAlias.c_str());
        AZ::IO::FileIOBase::GetInstance()->SetAlias("@assets@", assetsAlias.c_str());

        m_platformCatalogManager = AZStd::make_unique<AzToolsFramework::PlatformAddressedAssetCatalogManager>();

        InitArgValidationLists();

        switch (m_commandType)
        {
        case CommandType::Seeds:
            return RunSeedsCommands(ParseSeedsCommandData(parser));
        case CommandType::AssetLists:
            return RunAssetListsCommands(ParseAssetListsCommandData(parser));
        case CommandType::ComparisonRules:
            return RunComparisonRulesCommands(ParseComparisonRulesCommandData(parser));
        case CommandType::Compare:
            return RunCompareCommand(ParseCompareCommandData(parser));
        case CommandType::BundleSettings:
            return RunBundleSettingsCommands(ParseBundleSettingsCommandData(parser));
        case CommandType::Bundles:
            return RunBundlesCommands(ParseBundlesCommandData(parser));
        case CommandType::BundleSeed:
            return RunBundleSeedCommands(ParseBundleSeedCommandData(parser));
        }

        return false;
    }

    AZ::ComponentTypeList ApplicationManager::GetRequiredSystemComponents() const
    {
        AZ::ComponentTypeList components = AzFramework::Application::GetRequiredSystemComponents();

        components.emplace_back(azrtti_typeid<AzToolsFramework::AssetBundleComponent>());
        components.emplace_back(azrtti_typeid<AzToolsFramework::ArchiveComponent>());

        for (auto iter = components.begin(); iter != components.end();)
        {
            if (*iter == azrtti_typeid<AzFramework::GameEntityContextComponent>() ||
                *iter == azrtti_typeid<AzFramework::InputSystemComponent>() ||
                *iter == azrtti_typeid<AzFramework::DrillerNetworkAgentComponent>() ||
                *iter == azrtti_typeid<AZ::SliceSystemComponent>())
            {
                // Asset Bundler does not require the above components to be active
                iter = components.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        return components;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////
    // Get Generic Command Info
    ////////////////////////////////////////////////////////////////////////////////////////////

    CommandType ApplicationManager::GetCommandType(const AzFramework::CommandLine* parser, bool suppressErrors)
    {
        // Verify that the user has only typed in one sub-command
        size_t numMiscValues = parser->GetNumMiscValues();
        if (numMiscValues == 0)
        {
            AZ_Error(AppWindowName, suppressErrors, "Invalid command: Must provide a sub-command (ex: \"%s\").", AssetBundler::SeedsCommand);
            return CommandType::Invalid;
        }
        else if (numMiscValues > 1)
        {
            AZ_Error(AppWindowName, suppressErrors, "Invalid command: Cannot perform more than one sub-command operation at once");
            return CommandType::Invalid;
        }

        AZStd::string subCommand = parser->GetMiscValue(0);
        if (!azstricmp(subCommand.c_str(),AssetBundler::SeedsCommand))
        {
            return CommandType::Seeds;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::AssetListsCommand))
        {
            return CommandType::AssetLists;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::ComparisonRulesCommand))
        {
            return CommandType::ComparisonRules;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::CompareCommand))
        {
            return CommandType::Compare;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::BundleSettingsCommand))
        {
            return CommandType::BundleSettings;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::BundlesCommand))
        {
            return CommandType::Bundles;
        }
        else if (!azstricmp(subCommand.c_str(), AssetBundler::BundleSeedCommand))
        {
            return CommandType::BundleSeed;
        }
        else
        {
            AZ_Error(AppWindowName, false, "( %s ) is not a valid sub-command", subCommand);
            return CommandType::Invalid;
        }
    }

    bool ApplicationManager::ShouldPrintHelp(const AzFramework::CommandLine* parser)
    {
        return parser->HasSwitch(AssetBundler::HelpFlag) || parser->HasSwitch(AssetBundler::HelpFlagAlias);
    }

    bool ApplicationManager::ShouldPrintVerbose(const AzFramework::CommandLine* parser)
    {
        return parser->HasSwitch(AssetBundler::VerboseFlag);
    }

    void ApplicationManager::InitArgValidationLists()
    {
        m_allSeedsArgs = {
            SeedListFileArg,
            AddSeedArg,
            RemoveSeedArg,
            AddPlatformToAllSeedsFlag,
            RemovePlatformFromAllSeedsFlag,
            UpdateSeedPathArg,
            RemoveSeedPathArg,
            PrintFlag,
            PlatformArg,
            AssetCatalogFileArg,
            VerboseFlag
        };

        m_allAssetListsArgs = {
            AssetListFileArg,
            SeedListFileArg,
            AddSeedArg,
            AddDefaultSeedListFilesFlag,
            PlatformArg,
            AssetCatalogFileArg,
            PrintFlag,
            DryRunFlag,
            GenerateDebugFileFlag,
            AllowOverwritesFlag,
            VerboseFlag,
            SkipArg
        };

        m_allComparisonRulesArgs = {
            ComparisonRulesFileArg,
            ComparisonTypeArg,
            ComparisonFilePatternArg,
            ComparisonFilePatternTypeArg,
            AllowOverwritesFlag,
            VerboseFlag
        };

        m_allCompareArgs =
        {
            ComparisonRulesFileArg,
            ComparisonTypeArg,
            ComparisonFilePatternArg,
            ComparisonFilePatternTypeArg,
            CompareFirstFileArg,
            CompareSecondFileArg,
            CompareOutputFileArg,
            ComparePrintArg,
            IntersectionCountArg,
            AllowOverwritesFlag,
            VerboseFlag
        };

        m_allBundleSettingsArgs = {
            BundleSettingsFileArg,
            AssetListFileArg,
            OutputBundlePathArg,
            BundleVersionArg,
            MaxBundleSizeArg,
            PlatformArg,
            PrintFlag,
            VerboseFlag
        };

        m_allBundlesArgs = {
            BundleSettingsFileArg,
            AssetListFileArg,
            OutputBundlePathArg,
            BundleVersionArg,
            MaxBundleSizeArg,
            PlatformArg,
            AllowOverwritesFlag,
            VerboseFlag
        };

        m_allBundleSeedArgs = {
            BundleSettingsFileArg,
            AddSeedArg,
            OutputBundlePathArg,
            BundleVersionArg,
            MaxBundleSizeArg,
            PlatformArg,
            AssetCatalogFileArg,
            AllowOverwritesFlag,
            VerboseFlag
        };
    }


    ////////////////////////////////////////////////////////////////////////////////////////////
    // Store Detailed Command Info
    ////////////////////////////////////////////////////////////////////////////////////////////

    AZ::Outcome<SeedsParams, AZStd::string> ApplicationManager::ParseSeedsCommandData(const AzFramework::CommandLine* parser)
    {
        using namespace AzToolsFramework;

        auto validateArgsOutcome = ValidateInputArgs(parser, m_allSeedsArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpSeeds();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        SeedsParams params;

        // Read in Seed List Files arg
        auto requiredArgOutcome = GetFilePathArg(parser, SeedListFileArg, SeedsCommand, true);
        if (!requiredArgOutcome.IsSuccess())
        {
            return AZ::Failure(requiredArgOutcome.GetError());
        }
        // Seed List files do not have platform-specific file names
        params.m_seedListFile = FilePath(requiredArgOutcome.GetValue());

        // Read in Add/Remove Platform to All Seeds flag
        params.m_addPlatformToAllSeeds = parser->HasSwitch(AddPlatformToAllSeedsFlag);
        params.m_removePlatformFromAllSeeds = parser->HasSwitch(RemovePlatformFromAllSeedsFlag);
        AddMetric("AddPlatformToAllSeeds flag", params.m_addPlatformToAllSeeds);
        AddMetric("RemovePlatformFromAllSeeds flag", params.m_removePlatformFromAllSeeds);

        if (params.m_addPlatformToAllSeeds && params.m_removePlatformFromAllSeeds)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: Unable to run \"--%s\" and \"--%s\" at the same time.", AssetBundler::AddPlatformToAllSeedsFlag, AssetBundler::RemovePlatformFromAllSeedsFlag));
        }

        if ((params.m_addPlatformToAllSeeds || params.m_removePlatformFromAllSeeds) && !parser->HasSwitch(PlatformArg))
        {
            return AZ::Failure(AZStd::string::format("Invalid command: When running \"--%s\" or \"--%s\", the \"--%s\" arg is required.", AddPlatformToAllSeedsFlag, RemovePlatformFromAllSeedsFlag, PlatformArg));
        }

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }
        params.m_platformFlags = GetInputPlatformFlagsOrEnabledPlatformFlags(platformOutcome.GetValue());

        // Read in Asset Catalog File arg
        auto argOutcome = GetFilePathArg(parser, AssetCatalogFileArg, SeedsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.IsSuccess())
        {
            params.m_assetCatalogFile = FilePath(argOutcome.GetValue());
            AddFlagAttribute("AssetCatalogFile arg", true);
        }

        // Read in Add Seed arg
        params.m_addSeedList = GetAddSeedArgList(parser);

        // Read in Remove Seed arg
        size_t numRemoveSeedArgs = 0;
        if (parser->HasSwitch(RemoveSeedArg))
        {
            numRemoveSeedArgs = parser->GetNumSwitchValues(RemoveSeedArg);
            for (size_t removeSeedIndex = 0; removeSeedIndex < numRemoveSeedArgs; ++removeSeedIndex)
            {
                params.m_removeSeedList.push_back(parser->GetSwitchValue(RemoveSeedArg, removeSeedIndex));
            }
        }
        AddMetric("RemoveSeed arg size", aznumeric_cast<double>(numRemoveSeedArgs));

        // Read Update Seed Path arg
        params.m_updateSeedPathHint = parser->HasSwitch(UpdateSeedPathArg);
        AddMetric("UpdateSeedPathHint flag", params.m_updateSeedPathHint);

        // Read Update Seed Path arg
        params.m_removeSeedPathHint = parser->HasSwitch(RemoveSeedPathArg);
        AddMetric("RemoveSeedPathHint flag", params.m_removeSeedPathHint);

        // Read in Print flag
        params.m_print = parser->HasSwitch(PrintFlag);
        AddMetric("Print flag", params.m_print);

        return AZ::Success(params);
    }

    AZStd::string ApplicationManager::GetBinaryArgOptionFailure(const char* arg1, const char* arg2)
    {
        const char FailureMessage[] = "Missing argument: Either %s or %s must be supplied";
        return AZStd::string::format(FailureMessage, arg1, arg2);
    }

    AZ::Outcome<AssetListsParams, AZStd::string> ApplicationManager::ParseAssetListsCommandData(const AzFramework::CommandLine* parser)
    {
        auto validateArgsOutcome = ValidateInputArgs(parser, m_allAssetListsArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpAssetLists();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        AssetListsParams params;

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }
        params.m_platformFlags = GetInputPlatformFlagsOrEnabledPlatformFlags(platformOutcome.GetValue());

        // Read in Print flag
        params.m_print = parser->HasSwitch(PrintFlag);
        AddMetric("Print flag", params.m_print);

        // Read in Asset List File arg
        auto requiredArgOutcome = GetFilePathArg(parser, AssetListFileArg, AssetListsCommand, false);
        params.m_assetListFile = FilePath(requiredArgOutcome.GetValue());
       
        if (!params.m_print && !params.m_assetListFile.IsValid())
        {
            return AZ::Failure(GetBinaryArgOptionFailure(PrintFlag,AssetListFileArg));
        }

        // Read in Seed List File arg
        size_t numSeedListFiles = parser->GetNumSwitchValues(SeedListFileArg);
        for (size_t seedListFileIndex = 0; seedListFileIndex < numSeedListFiles; ++seedListFileIndex)
        {
            params.m_seedListFiles.emplace_back(FilePath(parser->GetSwitchValue(SeedListFileArg, seedListFileIndex)));
        }
        AddMetric("SeedListFile arg size", aznumeric_cast<double>(numSeedListFiles));

        // Read in Add Seed arg
        params.m_addSeedList = GetAddSeedArgList(parser);

        // Read in Skip arg
        params.m_skipList = GetSkipArgList(parser);

        // Read in Add Default Seed List Files arg
        params.m_addDefaultSeedListFiles = parser->HasSwitch(AddDefaultSeedListFilesFlag);
        AddMetric("AddDefaultSeedListFiles flag", params.m_addDefaultSeedListFiles);

        // Read in Asset Catalog File arg
        auto argOutcome = GetFilePathArg(parser, AssetCatalogFileArg, AssetListsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.IsSuccess())
        {
            params.m_assetCatalogFile = FilePath(argOutcome.GetValue());
            AddFlagAttribute("AssetCatalogFile arg", true);
        }

        // Read in Dry Run flag
        params.m_dryRun = parser->HasSwitch(DryRunFlag);
        AddMetric("Dry Run flag", params.m_dryRun);

        // Read in Generate Debug File flag
        params.m_generateDebugFile = parser->HasSwitch(GenerateDebugFileFlag);
        AddMetric("Generate Debug file flag", params.m_generateDebugFile);

        // Read in Allow Overwrites flag
        params.m_allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);
        AddMetric("Allow Overwrites flag", params.m_allowOverwrites);

        return AZ::Success(params);
    }

    AZ::Outcome<ComparisonRulesParams, AZStd::string> ApplicationManager::ParseComparisonRulesCommandData(const AzFramework::CommandLine* parser)
    {
        auto validateArgsOutcome = ValidateInputArgs(parser, m_allComparisonRulesArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpComparisonRules();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        ScopedTraceHandler traceHandler;
        ComparisonRulesParams params;

        auto requiredArgOutcome = GetFilePathArg(parser, ComparisonRulesFileArg, ComparisonRulesCommand, true);
        if (!requiredArgOutcome.IsSuccess())
        {
            return AZ::Failure(requiredArgOutcome.GetError());
        }
        params.m_comparisonRulesFile = FilePath(requiredArgOutcome.GetValue());

        if (params.m_comparisonRulesFile.AbsolutePath().empty())
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" cannot be empty.", ComparisonRulesFileArg));
        }
        
        auto parseComparisonTypesOutcome = ParseComparisonTypesAndPatterns(parser, params);
        if (!parseComparisonTypesOutcome.IsSuccess())
        {
            return AZ::Failure(parseComparisonTypesOutcome.GetError());
        }

        for (const auto& comparisonType : params.m_comparisonTypeList)
        {
            if (comparisonType == AzToolsFramework::AssetFileInfoListComparison::ComparisonType::IntersectionCount)
            {
                return AZ::Failure(AZStd::string::format("Adding compare operation ( %s ) to comparison rule file is not supported currently.", AzToolsFramework::AssetFileInfoListComparison::ComparisonTypeNames[aznumeric_cast<AZ::u8>(AzToolsFramework::AssetFileInfoListComparison::ComparisonType::IntersectionCount)]));
            }
        }

        // Read in Allow Overwrites flag
        params.m_allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);
        AddMetric("Allow Overwrites flag", params.m_allowOverwrites);

        return AZ::Success(params);
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::ParseComparisonTypesAndPatterns(const AzFramework::CommandLine* parser, ComparisonRulesParams& params)
    {
        int filePatternsConsumed = 0;
        size_t numComparisonTypes = parser->GetNumSwitchValues(ComparisonTypeArg);
        size_t numFilePatterns = parser->GetNumSwitchValues(ComparisonFilePatternArg);
        size_t numPatternTypes = parser->GetNumSwitchValues(ComparisonFilePatternTypeArg);

        size_t numIntersectionCount = parser->GetNumSwitchValues(IntersectionCountArg);

        if (numIntersectionCount > 1)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have exactly one value.", IntersectionCountArg));
        }

        params.m_intersectionCount = AZStd::stoi(parser->GetSwitchValue(IntersectionCountArg, 0));

        if (numPatternTypes != numFilePatterns)
        {
            return AZ::Failure(AZStd::string::format("Number of filePatternTypes ( %i ) and filePatterns ( %i ) must match.", numPatternTypes, numFilePatterns));
        }

        for (size_t comparisonTypeIndex = 0; comparisonTypeIndex < numComparisonTypes; ++comparisonTypeIndex)
        {
            auto comparisonTypeOutcome = ParseComparisonType(parser->GetSwitchValue(ComparisonTypeArg, comparisonTypeIndex));
            if (!comparisonTypeOutcome.IsSuccess())
            {
                return AZ::Failure(comparisonTypeOutcome.GetError());
            }

            auto comparisonType = comparisonTypeOutcome.GetValue();
            if (comparisonType == AzToolsFramework::AssetFileInfoListComparison::ComparisonType::FilePattern)
            {
                if (filePatternsConsumed >= numFilePatterns)
                {
                    return AZ::Failure(AZStd::string::format("Number of file patterns comparisons exceeded number of file patterns provided ( %i ).", numFilePatterns));
                }

                params.m_filePatternList.emplace_back(parser->GetSwitchValue(ComparisonFilePatternArg, filePatternsConsumed));

                auto filePatternTypeOutcome = ParseFilePatternType(parser->GetSwitchValue(ComparisonFilePatternTypeArg, filePatternsConsumed));
                if (!filePatternTypeOutcome.IsSuccess())
                {
                    return AZ::Failure(filePatternTypeOutcome.GetError());
                }
                params.m_filePatternTypeList.emplace_back(filePatternTypeOutcome.GetValue());
                filePatternsConsumed++;
            }
            else
            {
                params.m_filePatternList.emplace_back("");
                params.m_filePatternTypeList.emplace_back(AzToolsFramework::AssetFileInfoListComparison::FilePatternType::Default);
            }
            params.m_comparisonTypeList.emplace_back(comparisonType);
        }

        if (filePatternsConsumed != numFilePatterns)
        {
            return AZ::Failure(AZStd::string::format("Number of provided file patterns exceeded the number of file pattern comparisons ( %i ).", numFilePatterns));
        }

        return AZ::Success();
    }

    AZ::Outcome<ComparisonParams, AZStd::string> ApplicationManager::ParseCompareCommandData(const AzFramework::CommandLine* parser)
    {
        auto validateArgsOutcome = ValidateInputArgs(parser, m_allCompareArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpCompare();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        ComparisonParams params;

        AZStd::string inferredPlatform;
        // read in input files (first and second)
        for (size_t idx = 0; idx < parser->GetNumSwitchValues(CompareFirstFileArg); idx++)
        {
            AZStd::string value = parser->GetSwitchValue(CompareFirstFileArg, idx);
            if (!value.starts_with(compareVariablePrefix)) // Don't make this a path if it starts with the variable prefix
            {
                FilePath path = FilePath(value);
                value = path.AbsolutePath();
                inferredPlatform = AzToolsFramework::GetPlatformIdentifier(value);
            }
            params.m_firstCompareFile.emplace_back(AZStd::move(value));
        }

        for (size_t idx = 0; idx < parser->GetNumSwitchValues(CompareSecondFileArg); idx++)
        {
            AZStd::string value = parser->GetSwitchValue(CompareSecondFileArg, idx);
            if (!value.starts_with(compareVariablePrefix)) // Don't make this a path if it starts with the variable prefix
            {
                FilePath path = FilePath(value);
                value = path.AbsolutePath();
            }
            params.m_secondCompareFile.emplace_back(AZStd::move(value));
        }

        // read in output files
        for (size_t idx = 0; idx < parser->GetNumSwitchValues(CompareOutputFileArg); idx++)
        {
            AZStd::string value = parser->GetSwitchValue(CompareOutputFileArg, idx);
            if (!value.starts_with(compareVariablePrefix)) // Don't make this a path if it starts with the variable prefix
            {
                FilePath path = FilePath(value, inferredPlatform);
                value = path.AbsolutePath();
            }

            params.m_outputs.emplace_back(AZStd::move(value));
        }

        // Make Path object for existing rules file to load
        AZ::Outcome< AZStd::string, AZStd::string> pathArgOutcome = GetFilePathArg(parser, ComparisonRulesFileArg, CompareCommand, false);
        if (!pathArgOutcome.IsSuccess())
        {
            return AZ::Failure(pathArgOutcome.GetError());
        }

        params.m_comparisonRulesFile = FilePath(pathArgOutcome.GetValue());

        // Parse info for additional rules
        auto comparisonParseOutcome = ParseComparisonTypesAndPatterns(parser, params.m_comparisonRulesParams);
        if (!comparisonParseOutcome.IsSuccess())
        {
            return AZ::Failure(comparisonParseOutcome.GetError());
        }

        for (size_t idx = 0; idx < parser->GetNumSwitchValues(ComparePrintArg); idx++)
        {
            AZStd::string value = parser->GetSwitchValue(ComparePrintArg, idx);
            if (!value.starts_with(compareVariablePrefix)) // Don't make this a path if it starts with the variable prefix
            {
                FilePath path = FilePath(value);
                value = path.AbsolutePath();
            }
            params.m_printComparisons.emplace_back(AZStd::move(value));
        }

        params.m_printLast = parser->HasSwitch(ComparePrintArg) && params.m_printComparisons.empty();

        if (params.m_comparisonRulesParams.m_intersectionCount && (params.m_outputs.size() != 0 && params.m_outputs.size() != 1))
        {
            SendErrorMetricEvent("Invalid number of arguements for comparison outputs.");
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have either be 0 or 1 value for compare operation of type (%s).",
                CompareOutputFileArg, AzToolsFramework::AssetFileInfoListComparison::ComparisonTypeNames[aznumeric_cast<AZ::u8>(AzToolsFramework::AssetFileInfoListComparison::ComparisonType::IntersectionCount)]));
        }

        // Read in Allow Overwrites flag
        params.m_allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);
        AddMetric("Allow Overwrites flag", params.m_allowOverwrites);

        return AZ::Success(params);
    }

    AZ::Outcome<BundleSettingsParams, AZStd::string> ApplicationManager::ParseBundleSettingsCommandData(const AzFramework::CommandLine* parser)
    {
        auto validateArgsOutcome = ValidateInputArgs(parser, m_allBundleSettingsArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpBundleSettings();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        BundleSettingsParams params;

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }
        params.m_platformFlags = GetInputPlatformFlagsOrEnabledPlatformFlags(platformOutcome.GetValue());

        // Read in Bundle Settings File arg
        auto requiredArgOutcome = GetFilePathArg(parser, BundleSettingsFileArg, BundleSettingsCommand, true);
        if (!requiredArgOutcome.IsSuccess())
        {
            return AZ::Failure(requiredArgOutcome.GetError());
        }
        params.m_bundleSettingsFile = FilePath(requiredArgOutcome.GetValue());

        // Read in Asset List File arg
        auto argOutcome = GetFilePathArg(parser, AssetListFileArg, BundleSettingsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.GetValue().empty())
        {
            params.m_assetListFile = FilePath(argOutcome.GetValue());
            AddFlagAttribute("AssetListFile arg", true);
        }

        // Read in Output Bundle Path arg
        argOutcome = GetFilePathArg(parser, OutputBundlePathArg, BundleSettingsCommand);
        if (!argOutcome.IsSuccess())
        {
            return AZ::Failure(argOutcome.GetError());
        }
        if (!argOutcome.GetValue().empty())
        {
            params.m_outputBundlePath = FilePath(argOutcome.GetValue());
            AddFlagAttribute("OutputBundlePath arg", true);
        }

        // Read in Bundle Version arg
        if (parser->HasSwitch(BundleVersionArg))
        {
            if (parser->GetNumSwitchValues(BundleVersionArg) != 1)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have exactly one value.", BundleVersionArg));
            }
            params.m_bundleVersion = AZStd::stoi(parser->GetSwitchValue(BundleVersionArg, 0));
        }
        AddFlagAttribute("BundleVersion arg", parser->HasSwitch(BundleVersionArg));

        // Read in Max Bundle Size arg
        if (parser->HasSwitch(MaxBundleSizeArg))
        {
            if (parser->GetNumSwitchValues(MaxBundleSizeArg) != 1)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have exactly one value.", MaxBundleSizeArg));
            }
            params.m_maxBundleSizeInMB = AZStd::stoi(parser->GetSwitchValue(MaxBundleSizeArg, 0));
        }
        AddFlagAttribute("BundleVersion arg", parser->HasSwitch(MaxBundleSizeArg));

        // Read in Print flag
        params.m_print = parser->HasSwitch(PrintFlag);
        AddMetric("Print flag", params.m_print);

        return AZ::Success(params);
    }

    AZ::Outcome<BundlesParamsList, AZStd::string> ApplicationManager::ParseBundleSettingsAndOverrides(const AzFramework::CommandLine* parser, const char* commandName)
    {
        // Read in Bundle Settings File args
        auto bundleSettingsOutcome = GetArgsList<FilePath>(parser, BundleSettingsFileArg, commandName);
        if (!bundleSettingsOutcome.IsSuccess())
        {
            return AZ::Failure(bundleSettingsOutcome.GetError());
        }
        if (!bundleSettingsOutcome.GetValue().empty())
        {

            AddFlagAttribute("BundleSettingsFiles arg", true);
        }

        // Read in Asset List File args
        auto assetListOutcome = GetArgsList<FilePath>(parser, AssetListFileArg, commandName);
        if (!assetListOutcome.IsSuccess())
        {
            return AZ::Failure(assetListOutcome.GetError());
        }
        if (!assetListOutcome.GetValue().empty())
        {
            AddFlagAttribute("AssetListFiles arg", true);
        }

        // Read in Output Bundle Path args
        auto bundleOutputPathOutcome = GetArgsList<FilePath>(parser, OutputBundlePathArg, commandName);
        if (!bundleOutputPathOutcome.IsSuccess())
        {
            return AZ::Failure(bundleOutputPathOutcome.GetError());
        }
        if (!bundleOutputPathOutcome.GetValue().empty())
        {
            AddFlagAttribute("OutputBundlePaths arg", true);
        }

        AZStd::vector<FilePath> bundleSettingsFileList = bundleSettingsOutcome.TakeValue();
        AZStd::vector<FilePath> assetListFileList = assetListOutcome.TakeValue();
        AZStd::vector<FilePath> outputBundleFileList = bundleOutputPathOutcome.TakeValue();

        size_t bundleSettingListSize = bundleSettingsFileList.size();
        size_t assetFileListSize = assetListFileList.size();
        size_t outputBundleListSize = outputBundleFileList.size();


        // * We are validating the following cases here
        // * AssetFileList should always be equal to outputBundleList size even if they are of zero length.
        // * BundleSettingList can be a zero size list if the number of elements in assetFileList matches the number of elements in outputBundleList.
        // * If bundleSettingList contains non zero elements than either it should have the same number of elements as in assetFileList or the number of elements in assetFileList should be zero.
        if (bundleSettingListSize)
        {
            if (assetFileListSize != outputBundleListSize)
            {
                return AZ::Failure(AZStd::string::format("Invalid command:  \"--%s\" and \"--%s\" are required and should contain the same number of args.", AssetListFileArg, OutputBundlePathArg));
            }
            else if (bundleSettingListSize != assetFileListSize && assetFileListSize != 0)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\", \"--%s\" and \"--%s\" should contain the same number of args.", BundleSettingsFileArg, AssetListFileArg, OutputBundlePathArg));
            }
        }
        else
        {
            if (assetFileListSize != outputBundleListSize)
            {
                return AZ::Failure(AZStd::string::format("Invalid command:  \"--%s\" and \"--%s\" are required and should contain the same number of args.", AssetListFileArg, OutputBundlePathArg));
            }
        }

        size_t expectedListSize = AZStd::max(assetFileListSize, bundleSettingListSize);

        // Read in Bundle Version args
        auto bundleVersionOutcome = GetArgsList<AZStd::string>(parser, BundleVersionArg, commandName);
        if (!bundleVersionOutcome.IsSuccess())
        {
            return AZ::Failure(bundleVersionOutcome.GetError());
        }
        if (!bundleVersionOutcome.GetValue().empty())
        {
            AddFlagAttribute("BundleVersions args", true);
        }
        AZStd::vector<AZStd::string> bundleVersionList = bundleVersionOutcome.TakeValue();
        size_t bundleVersionListSize = bundleVersionList.size();

        if (bundleVersionListSize != expectedListSize && bundleVersionListSize >= 2)
        {
            if (expectedListSize != 1)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: Number of args in \"--%s\" can either be zero, one or %d. Actual size detected %d.", BundleVersionArg, expectedListSize, bundleVersionListSize));
            }
            else
            {
                return AZ::Failure(AZStd::string::format("Invalid command: Number of args in \"--%s\" is %d. Expected number of args is one.", BundleVersionArg, bundleVersionListSize ));
            }
        }

        // Read in Max Bundle Size args
        auto maxBundleSizeOutcome = GetArgsList<AZStd::string>(parser, MaxBundleSizeArg, commandName);
        if (!maxBundleSizeOutcome.IsSuccess())
        {
            return AZ::Failure(maxBundleSizeOutcome.GetError());
        }
        if (!maxBundleSizeOutcome.GetValue().empty())
        {
            AddFlagAttribute("Bundle Max Sizes args", true);
        }

        AZStd::vector<AZStd::string> maxBundleSizeList = maxBundleSizeOutcome.TakeValue();
        size_t maxBundleListSize = maxBundleSizeList.size();

        if (maxBundleListSize != expectedListSize && maxBundleListSize >= 2)
        {
            if (expectedListSize != 1)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: Number of args in \"--%s\" can either be zero, one or %d. Actual size detected %d.", MaxBundleSizeArg, expectedListSize, maxBundleListSize));
            }
            else
            {
                return AZ::Failure(AZStd::string::format("Invalid command: Number of args in \"--%s\" is %d. Expected number of args is one.", MaxBundleSizeArg, maxBundleListSize));
            }
        }

        // Read in Platform arg
        auto platformOutcome = GetPlatformArg(parser);
        if (!platformOutcome.IsSuccess())
        {
            return AZ::Failure(platformOutcome.GetError());
        }

        // Read in Allow Overwrites flag
        bool allowOverwrites = parser->HasSwitch(AllowOverwritesFlag);
        AddMetric("Allow Overwrites flag", allowOverwrites);
        BundlesParamsList bundleParamsList;

        for (int idx = 0; idx < expectedListSize; idx++)
        {
            BundlesParams bundleParams;
            bundleParams.m_bundleSettingsFile = bundleSettingListSize ? bundleSettingsFileList[idx] : FilePath();
            bundleParams.m_assetListFile = assetFileListSize ? assetListFileList[idx] : FilePath();
            bundleParams.m_outputBundlePath = outputBundleListSize ? outputBundleFileList[idx] : FilePath();
            if (bundleVersionListSize)
            {
                bundleParams.m_bundleVersion = bundleVersionListSize == 1 ? AZStd::stoi(bundleVersionList[0]) : AZStd::stoi(bundleVersionList[idx]);
            }

            if (maxBundleListSize)
            {
                bundleParams.m_maxBundleSizeInMB = maxBundleListSize == 1 ? AZStd::stoi(maxBundleSizeList[0]) : AZStd::stoi(maxBundleSizeList[idx]);
            }

            bundleParams.m_platformFlags = platformOutcome.GetValue();
            bundleParams.m_allowOverwrites = allowOverwrites;
            bundleParamsList.emplace_back(bundleParams);
        }

        return AZ::Success(bundleParamsList);
    }

    AZ::Outcome<BundlesParamsList, AZStd::string> ApplicationManager::ParseBundlesCommandData(const AzFramework::CommandLine* parser)
    {
        auto validateArgsOutcome = ValidateInputArgs(parser, m_allBundlesArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpBundles();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        auto parseSettingsOutcome = ParseBundleSettingsAndOverrides(parser, BundlesCommand);
        if (!parseSettingsOutcome.IsSuccess())
        {
            return AZ::Failure(parseSettingsOutcome.GetError());
        }

        return AZ::Success(parseSettingsOutcome.TakeValue());
    }

    AZ::Outcome<BundleSeedParams, AZStd::string> ApplicationManager::ParseBundleSeedCommandData(const AzFramework::CommandLine* parser)
    {

        auto validateArgsOutcome = ValidateInputArgs(parser, m_allBundleSeedArgs);
        if (!validateArgsOutcome.IsSuccess())
        {
            OutputHelpBundles();
            return AZ::Failure(validateArgsOutcome.TakeError());
        }

        BundleSeedParams params;

        params.m_addSeedList = GetAddSeedArgList(parser);
        auto parseSettingsOutcome = ParseBundleSettingsAndOverrides(parser, BundleSeedCommand);
        if (!parseSettingsOutcome.IsSuccess())
        {
            return AZ::Failure(parseSettingsOutcome.GetError());
        }
        BundlesParamsList paramsList = parseSettingsOutcome.TakeValue();

        params.m_bundleParams = paramsList[0];

        return AZ::Success(params);
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::ValidateInputArgs(const AzFramework::CommandLine* parser, const AZStd::vector<const char*>& validArgList)
    {
        for (const auto& paramInfo : parser->GetSwitchList())
        {
            bool isValidArg = false;

            for (const auto& validArg : validArgList)
            {
                if (AzFramework::StringFunc::Equal(paramInfo.first.c_str(), validArg))
                {
                    isValidArg = true;
                    break;
                }
            }

            if (!isValidArg)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" is not a valid argument for this sub-command.", paramInfo.first.c_str()));
            }
        }

        return AZ::Success();
    }

    AZ::Outcome<AZStd::string, AZStd::string> ApplicationManager::GetFilePathArg(const AzFramework::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired)
    {
        if (!parser->HasSwitch(argName))
        {
            if (isRequired)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" is required when running \"%s\".", argName, subCommandName));
            }
            return AZ::Success(AZStd::string());
        }

        if (parser->GetNumSwitchValues(argName) != 1)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have exactly one value.", argName));
        }

        return AZ::Success(parser->GetSwitchValue(argName, 0));
    }

    template <typename T>
    AZ::Outcome<AZStd::vector<T>, AZStd::string> ApplicationManager::GetArgsList(const AzFramework::CommandLine* parser, const char* argName, const char* subCommandName, bool isRequired)
    {
        AZStd::vector<T> args;

        if (!parser->HasSwitch(argName))
        {
            if (isRequired)
            {
                return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" is required when running \"%s\".", argName, subCommandName));
            }

            return AZ::Success(args);
        }

        size_t numValues = parser->GetNumSwitchValues(argName);

        for (int idx = 0; idx < numValues; ++idx)
        {
            args.emplace_back(T(parser->GetSwitchValue(argName, idx)));
        }

        return AZ::Success(args);
    }

    AZ::Outcome<AzFramework::PlatformFlags, AZStd::string> ApplicationManager::GetPlatformArg(const AzFramework::CommandLine* parser)
    {
        using namespace AzFramework;
        PlatformFlags platform = AzFramework::PlatformFlags::Platform_NONE;
        if (!parser->HasSwitch(PlatformArg))
        {
            return AZ::Success(platform);
        }

        size_t numValues = parser->GetNumSwitchValues(PlatformArg);
        if (numValues <= 0)
        {
            return AZ::Failure(AZStd::string::format("Invalid command: \"--%s\" must have at least one value.", PlatformArg));
        }
        AddFlagAttribute("Num Platform args", numValues);

        for (int platformIdx = 0; platformIdx < numValues; ++platformIdx)
        {
            AZStd::string platformStr = parser->GetSwitchValue(PlatformArg, platformIdx);
            platform |= AzFramework::PlatformHelper::GetPlatformFlag(platformStr);
        }

        return AZ::Success(platform);
    }

    AzFramework::PlatformFlags ApplicationManager::GetInputPlatformFlagsOrEnabledPlatformFlags(AzFramework::PlatformFlags inputPlatformFlags)
    {
        using namespace AzToolsFramework;
        if (inputPlatformFlags != AzFramework::PlatformFlags::Platform_NONE)
        {
            return inputPlatformFlags;
        }

        // If no platform was specified, defaulting to platforms specified in the asset processor config files
        const char* appRoot = nullptr;
        AzFramework::ApplicationRequests::Bus::BroadcastResult(appRoot, &AzFramework::ApplicationRequests::GetAppRoot);

        AzFramework::PlatformFlags platformFlags = GetEnabledPlatformFlags(g_cachedEngineRoot, appRoot, m_currentProjectName.c_str());

        AZStd::vector<AZStd::string> platformNames = AzFramework::PlatformHelper::GetPlatforms(platformFlags);
        AZStd::string platformsString;
        AzFramework::StringFunc::Join(platformsString, platformNames.begin(), platformNames.end(), ", ");

        AZ_TracePrintf(AppWindowName, "No platform specified, defaulting to platforms ( %s ).\n", platformsString.c_str());
        return platformFlags;
    }

    AZStd::vector<AZStd::string> ApplicationManager::GetAddSeedArgList(const AzFramework::CommandLine* parser)
    {
        AZStd::vector<AZStd::string> addSeedList;
        size_t numAddSeedArgs = parser->GetNumSwitchValues(AddSeedArg);
        for (size_t addSeedIndex = 0; addSeedIndex < numAddSeedArgs; ++addSeedIndex)
        {
            addSeedList.push_back(parser->GetSwitchValue(AddSeedArg, addSeedIndex));
        }
        AddMetric("AddSeed arg size", aznumeric_cast<double>(numAddSeedArgs));
        return addSeedList;
    }

    AZStd::vector<AZStd::string> ApplicationManager::GetSkipArgList(const AzFramework::CommandLine* parser)
    {
        AZStd::vector<AZStd::string> skipList;
        size_t numArgs = parser->GetNumSwitchValues(SkipArg);
        for (size_t argIndex = 0; argIndex < numArgs; ++argIndex)
        {
            skipList.push_back(parser->GetSwitchValue(SkipArg, argIndex));
        }
        AddMetric("Skip arg size", aznumeric_cast<double>(numArgs));
        return skipList;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Run Commands
    ////////////////////////////////////////////////////////////////////////////////////////////

    bool ApplicationManager::RunSeedsCommands(const AZ::Outcome<SeedsParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzFramework;
        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        SeedsParams params = paramsOutcome.GetValue();

        // Asset Catalog
        auto catalogOutcome = InitAssetCatalog(params.m_platformFlags, params.m_assetCatalogFile.AbsolutePath());
        if (!catalogOutcome.IsSuccess())
        {
            // Metric event has already been sent
            AZ_Error(AppWindowName, false, catalogOutcome.GetError().c_str());
            return false;
        }

        // Seed List File
        auto seedOutcome = LoadSeedListFile(params.m_seedListFile.AbsolutePath(), params.m_platformFlags);
        if (!seedOutcome.IsSuccess())
        {
            // Metric event has already been sent
            AZ_Error(AppWindowName, false, seedOutcome.GetError().c_str());
            return false;
        }

        for (const PlatformId platformId : AzFramework::PlatformHelper::GetPlatformIndices(params.m_platformFlags))
        {
            // Add Seeds
            PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);
            for (const AZStd::string& assetPath : params.m_addSeedList)
            {
                m_assetSeedManager->AddSeedAsset(assetPath, platformFlag);
            }

            // Remove Seeds
            for (const AZStd::string& assetPath : params.m_removeSeedList)
            {
                m_assetSeedManager->RemoveSeedAsset(assetPath, platformFlag);
            }

            // Add Platform to All Seeds
            if (params.m_addPlatformToAllSeeds)
            {
                m_assetSeedManager->AddPlatformToAllSeeds(platformId);
            }

            // Remove Platform from All Seeds
            if (params.m_removePlatformFromAllSeeds)
            {
                m_assetSeedManager->RemovePlatformFromAllSeeds(platformId);
            }
        }

        if (params.m_updateSeedPathHint)
        {
            m_assetSeedManager->UpdateSeedPath();
        }

        if (params.m_removeSeedPathHint)
        {
            m_assetSeedManager->RemoveSeedPath();
        }

        if (params.m_print)
        {
            PrintSeedList(params.m_seedListFile.AbsolutePath());
        }

        // Save
        AZ_TracePrintf(AssetBundler::AppWindowName, "Saving Seed List to ( %s )...\n", params.m_seedListFile.AbsolutePath().c_str());
        if (!m_assetSeedManager->Save(params.m_seedListFile.AbsolutePath()))
        {
            SendErrorMetricEvent("Failed to save seed list file.");
            AZ_Error(AssetBundler::AppWindowName, false, "Unable to save Seed List to ( %s ).", params.m_seedListFile.AbsolutePath().c_str());
            return false;
        }

        AZ_TracePrintf(AssetBundler::AppWindowName, "Save successful!\n");

        return true;
    }

    bool ApplicationManager::RunAssetListsCommands(const AZ::Outcome<AssetListsParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzFramework;
        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        AssetListsParams params = paramsOutcome.GetValue();

        // Asset Catalog
        auto catalogOutcome = InitAssetCatalog(params.m_platformFlags, params.m_assetCatalogFile.AbsolutePath());
        if (!catalogOutcome.IsSuccess())
        {
            // Metric event has already been sent
            AZ_Error(AppWindowName, false, catalogOutcome.GetError().c_str());
            return false;
        }

        // Seed List Files
        AZ::Outcome<void, AZStd::string> seedListOutcome;
        AZStd::string seedListFileAbsolutePath;
        for (const FilePath& seedListFile : params.m_seedListFiles)
        {
            seedListFileAbsolutePath = seedListFile.AbsolutePath();
            if (!AZ::IO::FileIOBase::GetInstance()->Exists(seedListFileAbsolutePath.c_str()))
            {
                SendErrorMetricEvent("Seed List file does not exist.");
                AZ_Error(AppWindowName, false, "Cannot load Seed List file ( %s ): File does not exist.\n", seedListFileAbsolutePath.c_str());
                return false;
            }

            seedListOutcome = LoadSeedListFile(seedListFileAbsolutePath, params.m_platformFlags);
            if (!seedListOutcome.IsSuccess())
            {
                // Metric event has already been sent
                AZ_Error(AppWindowName, false, seedListOutcome.GetError().c_str());
                return false;
            }
        }

        // Add Default Seed List Files
        if (params.m_addDefaultSeedListFiles)
        {
            AZStd::vector<AZStd::string> defaultSeedListFiles = GetDefaultSeedListFiles(AssetBundler::g_cachedEngineRoot, m_gemInfoList, params.m_platformFlags);
            for (const AZStd::string& seedListFile : defaultSeedListFiles)
            {
                seedListOutcome = LoadSeedListFile(seedListFile, params.m_platformFlags);
                if (!seedListOutcome.IsSuccess())
                {
                    // Metric event has already been sent
                    AZ_Error(AppWindowName, false, seedListOutcome.GetError().c_str());
                    return false;
                }
            }

            seedListOutcome = LoadProjectDependenciesFile(params.m_platformFlags);
            if (!seedListOutcome.IsSuccess())
            {
                // Metric event has already been sent
                AZ_Error(AppWindowName, false, seedListOutcome.GetError().c_str());
                return false;
            }
        }

        if (!RunPlatformSpecificAssetListCommands(params, params.m_platformFlags))
        {
            // Errors and metrics have already been sent
            return false;
        }

        return true;
    }

    bool ApplicationManager::RunComparisonRulesCommands(const AZ::Outcome<ComparisonRulesParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;

        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        ComparisonRulesParams params = paramsOutcome.GetValue();
        AssetFileInfoListComparison assetFileInfoListComparison;

        ConvertRulesParamsToComparisonData(params, assetFileInfoListComparison);

        // Check if we are performing a destructive overwrite that the user did not approve 
        if (!params.m_allowOverwrites && AZ::IO::FileIOBase::GetInstance()->Exists(params.m_comparisonRulesFile.AbsolutePath().c_str()))
        {
            SendErrorMetricEvent("Unapproved destructive overwrite on an Comparison Rules file.");
            AZ_Error(AssetBundler::AppWindowName, false, "Comparison Rules file ( %s ) already exists, running this command would perform a destructive overwrite.\n\n"
                "Run your command again with the ( --%s ) arg if you want to save over the existing file.", params.m_comparisonRulesFile.AbsolutePath().c_str(), AllowOverwritesFlag);
            return false;
        }

        // Attempt to save
        AZ_TracePrintf(AssetBundler::AppWindowName, "Saving Comparison Rules file to ( %s )...\n", params.m_comparisonRulesFile.AbsolutePath().c_str());
        if (!assetFileInfoListComparison.Save(params.m_comparisonRulesFile.AbsolutePath().c_str()))
        {
            SendErrorMetricEvent("Failed to save comparison rules file.");
            AZ_Error(AssetBundler::AppWindowName, false, "Failed to save Comparison Rules file ( %s ).", params.m_comparisonRulesFile.AbsolutePath().c_str());
            return false;
        }
        AZ_TracePrintf(AssetBundler::AppWindowName, "Save successful!\n");

        return true;
    }

    void ApplicationManager::ConvertRulesParamsToComparisonData(const ComparisonRulesParams& params, AzToolsFramework::AssetFileInfoListComparison& assetListComparison)
    {
        using namespace AzToolsFramework;

        for (int idx = 0; idx < params.m_comparisonTypeList.size(); idx++)
        {
            AssetFileInfoListComparison::ComparisonData comparisonData;
            comparisonData.m_comparisonType = params.m_comparisonTypeList[idx];
            comparisonData.m_filePattern = params.m_filePatternList[idx];
            comparisonData.m_filePatternType = params.m_filePatternTypeList[idx];
            comparisonData.m_intersectionCount = params.m_intersectionCount;

            assetListComparison.AddComparisonStep(comparisonData);
        }
    }

    bool ApplicationManager::RunCompareCommand(const AZ::Outcome<ComparisonParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;

        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        ComparisonParams params = paramsOutcome.GetValue();
        AssetFileInfoListComparison comparisonOperations;

        // Load comparison rules from file if one was provided
        if (!params.m_comparisonRulesFile.AbsolutePath().empty())
        {

            auto rulesFileLoadResult = AssetFileInfoListComparison::Load(params.m_comparisonRulesFile.AbsolutePath());
            if (!rulesFileLoadResult.IsSuccess())
            {
                SendErrorMetricEvent("Failed to load comparison rules file.");
                AZ_Error(AppWindowName, false, rulesFileLoadResult.GetError().c_str());
                return false;
            }
            comparisonOperations = rulesFileLoadResult.GetValue();
        }

        // generate comparisons from additional commands and add it to comparisonOperations
        ConvertRulesParamsToComparisonData(params.m_comparisonRulesParams, comparisonOperations);

        int expectedSecondInputs = 0;

        if (params.m_comparisonRulesParams.m_intersectionCount)
        {
            if ((comparisonOperations.GetComparisonList().size() == 1) && comparisonOperations.GetComparisonList()[0].m_comparisonType != AssetFileInfoListComparison::ComparisonType::IntersectionCount)
            {
                SendErrorMetricEvent("Invalid arguement provided for IntersectionCount operation.");
                AZ_Error(AppWindowName, false, "Invalid arguement detected. Command ( --%s ) is incompatible with compare operation of type (%s).",
                    IntersectionCountArg, AssetFileInfoListComparison::ComparisonTypeNames[aznumeric_cast<AZ::u8>(comparisonOperations.GetComparisonList()[0].m_comparisonType)]);
                return false;
            }
            // Since IntersectionCount Operation cannot be combined with other operation Comparison List should be 1
            else if (comparisonOperations.GetComparisonList().size() > 1)
            {
                SendErrorMetricEvent("Intersection operation cannot be combined with other comparison operations.");
                AZ_Error(AppWindowName, false, "Compare operation of type ( %s ) cannot be combined with other comparison operations. Number of comparison operation detected (%d).",
                    AssetFileInfoListComparison::ComparisonTypeNames[aznumeric_cast<AZ::u8>(AssetFileInfoListComparison::ComparisonType::IntersectionCount)], comparisonOperations.GetComparisonList().size());
                return false;
            }

            if (params.m_outputs.size())
            {
                comparisonOperations.SetDestinationPath(0, params.m_outputs[0]);
            }
        }
        else
        {
            // Verify that inputs match # and content of comparisons
            if (comparisonOperations.GetComparisonList().size() != params.m_firstCompareFile.size())
            {
                SendErrorMetricEvent("Mismatch in number of comparisons and comparison files.");
                AZ_Error(AppWindowName, false, "The number of ( --%s ) values must be the same as the number of comparisons defined ( %u ).", CompareFirstFileArg, comparisonOperations.GetComparisonList().size());
                return false;
            }

            // Verify that outputs match # and content of comparisons
            if (comparisonOperations.GetComparisonList().size() != params.m_outputs.size())
            {
                SendErrorMetricEvent("Mismatch in number of comparisons and comparison outputs.");
                AZ_Error(AppWindowName, false, "The number of ( --%s ) values must be the same as the number of comparisons defined ( %i ).", CompareOutputFileArg, comparisonOperations.GetComparisonList().size());
                return false;
            }

            for (int idx = 0; idx < comparisonOperations.GetComparisonList().size(); idx++)
            {
                comparisonOperations.SetDestinationPath(idx, params.m_outputs[idx]);
                if (comparisonOperations.GetComparisonList()[idx].m_comparisonType != AssetFileInfoListComparison::ComparisonType::FilePattern) // file pattern operations do not use a second input file
                {
                    expectedSecondInputs++;
                }
            }
        }

        if (params.m_secondCompareFile.size() != expectedSecondInputs)
        {
            SendErrorMetricEvent("Mismatch in number of comparisons and second comparison files.");
            AZ_Error(AppWindowName, false, "The number of ( --%s ) values must be the same as the number of comparisons that require two files ( %i ).", CompareSecondFileArg, expectedSecondInputs);
            return false;
        }

        auto compareOutcome = comparisonOperations.Compare(params.m_firstCompareFile, params.m_secondCompareFile);
        if (!compareOutcome.IsSuccess())
        {
            SendErrorMetricEvent("Comparison operation failed");
            AZ_Error(AppWindowName, false, compareOutcome.GetError().c_str());
            return false;
        }

        if (params.m_printLast)
        {
            PrintComparisonAssetList(compareOutcome.GetValue(), params.m_outputs.size() ? params.m_outputs.back() : "");
        }

        // Check if we are performing a destructive overwrite that the user did not approve 
        if (!params.m_allowOverwrites)
        {
            AZStd::vector<AZStd::string> destructiveOverwriteFilePaths = comparisonOperations.GetDestructiveOverwriteFilePaths();
            if (!destructiveOverwriteFilePaths.empty())
            {
                SendErrorMetricEvent("Unapproved destructive overwrite on an Asset List file (Comparison).");
                for (const AZStd::string& path : destructiveOverwriteFilePaths)
                {
                    AZ_Error(AssetBundler::AppWindowName, false, "Asset List file ( %s ) already exists, running this command would perform a destructive overwrite.", path.c_str());
                }
                AZ_Printf(AssetBundler::AppWindowName, "\nRun your command again with the ( --%s ) arg if you want to save over the existing file.\n\n", AllowOverwritesFlag)
                return false;
            }
        }

        AZ_Printf(AssetBundler::AppWindowName, "Saving results of comparison operation...\n");
        auto saveOutcome = comparisonOperations.SaveResults();
        if (!saveOutcome.IsSuccess())
        {
            SendErrorMetricEvent(saveOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, saveOutcome.GetError().c_str());
            return false;
        }
        AZ_Printf(AssetBundler::AppWindowName, "Save successful!\n");

        for (const AZStd::string& comparisonKey : params.m_printComparisons)
        {
            PrintComparisonAssetList(comparisonOperations.GetComparisonResults(comparisonKey), comparisonKey);
        }

        return true;
    }

    void ApplicationManager::PrintComparisonAssetList(const AzToolsFramework::AssetFileInfoList& infoList, const AZStd::string& resultName)
    {
        using namespace AzToolsFramework;

        if (infoList.m_fileInfoList.size() == 0)
        {
            return;
        }

        AZ_Printf(AssetBundler::AppWindowName, "Printing assets from the comparison result %s.\n", resultName.c_str());
        AZ_Printf(AssetBundler::AppWindowName, "------------------------------------------\n");

        for (const AssetFileInfo& assetFilenfo : infoList.m_fileInfoList)
        {
            AZ_Printf(AssetBundler::AppWindowName, "- %s\n", assetFilenfo.m_assetRelativePath.c_str());
        }

        AZ_Printf(AssetBundler::AppWindowName, "Total number of assets (%u).\n", infoList.m_fileInfoList.size());
        AZ_Printf(AssetBundler::AppWindowName, "---------------------------\n");
    }

    bool ApplicationManager::RunBundleSettingsCommands(const AZ::Outcome<BundleSettingsParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;
        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        BundleSettingsParams params = paramsOutcome.GetValue();

        for (const AZStd::string& platformName : AzFramework::PlatformHelper::GetPlatformsInterpreted(params.m_platformFlags))
        {
            AssetBundleSettings bundleSettings;

            // Attempt to load Bundle Settings file. If the load operation fails, we are making a new file and there is no need to error.
            FilePath platformSpecificBundleSettingsFilePath = FilePath(params.m_bundleSettingsFile.AbsolutePath(), platformName);
            AZ::Outcome<AssetBundleSettings, AZStd::string> loadBundleSettingsOutcome = AssetBundleSettings::Load(platformSpecificBundleSettingsFilePath.AbsolutePath());
            if (loadBundleSettingsOutcome.IsSuccess())
            {
                bundleSettings = loadBundleSettingsOutcome.TakeValue();
            }

            // Asset List File
            AZStd::string assetListFilePath = FilePath(params.m_assetListFile.AbsolutePath(), platformName).AbsolutePath();
            if (!assetListFilePath.empty())
            {
                if (!AzFramework::StringFunc::EndsWith(assetListFilePath, AssetSeedManager::GetAssetListFileExtension()))
                {
                    SendErrorMetricEvent("Asset List file has the wrong extension.");
                    AZ_Error(AppWindowName, false, "Cannot set Asset List file to ( %s ): file extension must be ( %s ).", assetListFilePath.c_str(), AssetSeedManager::GetAssetListFileExtension());
                    return false;
                }

                if (!AZ::IO::FileIOBase::GetInstance()->Exists(assetListFilePath.c_str()))
                {
                    SendErrorMetricEvent("Asset List file does not exist.");
                    AZ_Error(AppWindowName, false, "Cannot set Asset List file to ( %s ): file does not exist.", assetListFilePath.c_str());
                    return false;
                }

                // Make the path relative to the engine root folder before saving
                AzFramework::StringFunc::Replace(assetListFilePath, GetEngineRoot(), "");

                bundleSettings.m_assetFileInfoListPath = assetListFilePath;
            }

            // Output Bundle Path
            AZStd::string outputBundlePath = FilePath(params.m_outputBundlePath.AbsolutePath(), platformName).AbsolutePath();
            if (!outputBundlePath.empty())
            {
                if (!AzFramework::StringFunc::EndsWith(outputBundlePath, AssetBundleSettings::GetBundleFileExtension()))
                {
                    SendErrorMetricEvent("Output Bundle File Path has the wrong extension.");
                    AZ_Error(AppWindowName, false, "Cannot set Output Bundle Path to ( %s ): file extension must be ( %s ).", outputBundlePath.c_str(), AssetBundleSettings::GetBundleFileExtension());
                    return false;
                }

                // Make the path relative to the engine root folder before saving
                AzFramework::StringFunc::Replace(outputBundlePath, GetEngineRoot(), "");

                bundleSettings.m_bundleFilePath = outputBundlePath;
            }

            // Bundle Version
            if (params.m_bundleVersion > 0 && params.m_bundleVersion <= AzFramework::AssetBundleManifest::CurrentBundleVersion)
            {
                bundleSettings.m_bundleVersion = params.m_bundleVersion;
            }

            // Max Bundle Size (in MB)
            if (params.m_maxBundleSizeInMB > 0 && params.m_maxBundleSizeInMB <= AssetBundleSettings::GetMaxBundleSizeInMB())
            {
                bundleSettings.m_maxBundleSizeInMB = params.m_maxBundleSizeInMB;
            }

            // Print
            if (params.m_print)
            {
                AZ_TracePrintf(AssetBundler::AppWindowName, "\nContents of Bundle Settings file ( %s ):\n", platformSpecificBundleSettingsFilePath.AbsolutePath().c_str());
                AZ_TracePrintf(AssetBundler::AppWindowName, "    Platform: %s\n", platformName.c_str());
                AZ_TracePrintf(AssetBundler::AppWindowName, "    Asset List file: %s\n", bundleSettings.m_assetFileInfoListPath.c_str());
                AZ_TracePrintf(AssetBundler::AppWindowName, "    Output Bundle path: %s\n", bundleSettings.m_bundleFilePath.c_str());
                AZ_TracePrintf(AssetBundler::AppWindowName, "    Bundle Version: %i\n", bundleSettings.m_bundleVersion);
                AZ_TracePrintf(AssetBundler::AppWindowName, "    Max Bundle Size: %u MB\n\n", bundleSettings.m_maxBundleSizeInMB);
            }

            // Save
            AZ_TracePrintf(AssetBundler::AppWindowName, "Saving Bundle Settings file to ( %s )...\n", platformSpecificBundleSettingsFilePath.AbsolutePath().c_str());

            if (!AssetBundleSettings::Save(bundleSettings, platformSpecificBundleSettingsFilePath.AbsolutePath()))
            {
                SendErrorMetricEvent("Failed to save Bundle Settings file.");
                AZ_Error(AssetBundler::AppWindowName, false, "Unable to save Bundle Settings file to ( %s ).", platformSpecificBundleSettingsFilePath.AbsolutePath().c_str());
                return false;
            }

            AZ_TracePrintf(AssetBundler::AppWindowName, "Save successful!\n");
        }

        return true;
    }

    bool ApplicationManager::RunBundlesCommands(const AZ::Outcome<BundlesParamsList, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;
        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        BundlesParamsList paramsList = paramsOutcome.GetValue();
        AZStd::vector<AZStd::pair<AssetBundleSettings, BundlesParams>> allBundleSettings;
        for (BundlesParams& params : paramsList)
        {
            // If no platform was input we want to loop over all possible platforms and make bundles for whatever we find
            if (params.m_platformFlags == AzFramework::PlatformFlags::Platform_NONE)
            {
                params.m_platformFlags = AzFramework::PlatformFlags::AllNamedPlatforms;
            }

            // Load or generate Bundle Settings
            AzFramework::PlatformFlags allPlatformsInBundle = AzFramework::PlatformFlags::Platform_NONE;
            if (params.m_bundleSettingsFile.AbsolutePath().empty())
            {
                // Verify input file path formats before looking for platform-specific versions 
                auto fileExtensionOutcome = AssetSeedManager::ValidateAssetListFileExtension(params.m_assetListFile.AbsolutePath());
                if (!fileExtensionOutcome.IsSuccess())
                {
                    SendErrorMetricEvent("Invalid Asset List file extension");
                    AZ_Error(AssetBundler::AppWindowName, false, fileExtensionOutcome.GetError().c_str());
                    return false;
                }

                AZStd::vector<FilePath> allAssetListFilePaths = GetAllPlatformSpecificFilesOnDisk(params.m_assetListFile, params.m_platformFlags);

                // Create temporary Bundle Settings structs for every Asset List file
                for (const auto& assetListFilePath : allAssetListFilePaths)
                {
                    AssetBundleSettings bundleSettings;
                    bundleSettings.m_assetFileInfoListPath = assetListFilePath.AbsolutePath();
                    bundleSettings.m_platform = GetPlatformIdentifier(assetListFilePath.AbsolutePath());
                    allPlatformsInBundle |= AzFramework::PlatformHelper::GetPlatformFlag(bundleSettings.m_platform);
                    allBundleSettings.emplace_back(AZStd::make_pair(bundleSettings, params));
                }
            }
            else
            {
                // Verify input file path formats before looking for platform-specific versions 
                auto fileExtensionOutcome = AssetBundleSettings::ValidateBundleSettingsFileExtension(params.m_bundleSettingsFile.AbsolutePath());
                if (!fileExtensionOutcome.IsSuccess())
                {
                    SendErrorMetricEvent("Invalid Bundle Settings file extension");
                    AZ_Error(AssetBundler::AppWindowName, false, fileExtensionOutcome.GetError().c_str());
                    return false;
                }

                AZStd::vector<FilePath> allBundleSettingsFilePaths = GetAllPlatformSpecificFilesOnDisk(params.m_bundleSettingsFile, params.m_platformFlags);

                // Attempt to load all Bundle Settings files (there may be one or many to load)
                for (const auto& bundleSettingsFilePath : allBundleSettingsFilePaths)
                {
                    AZ::Outcome<AssetBundleSettings, AZStd::string> loadBundleSettingsOutcome = AssetBundleSettings::Load(bundleSettingsFilePath.AbsolutePath());
                    if (!loadBundleSettingsOutcome.IsSuccess())
                    {
                        SendErrorMetricEvent("Failed to load Bundle Settings file.");
                        AZ_Error(AssetBundler::AppWindowName, false, loadBundleSettingsOutcome.GetError().c_str());
                        return false;
                    }

                    allBundleSettings.emplace_back(AZStd::make_pair(loadBundleSettingsOutcome.TakeValue(), params));
                    allPlatformsInBundle |= AzFramework::PlatformHelper::GetPlatformFlag(allBundleSettings.back().first.m_platform);
                }
            }
        }

        AZStd::atomic_uint failureCount = 0;

        // Create all Bundles
        AZ::parallel_for_each(allBundleSettings.begin(), allBundleSettings.end(), [this, &failureCount](AZStd::pair<AzToolsFramework::AssetBundleSettings, BundlesParams> bundleSettings)
            {
                BundlesParams params = bundleSettings.second;
                auto overrideOutcome = ApplyBundleSettingsOverrides(
                    bundleSettings.first,
                    params.m_assetListFile.AbsolutePath(),
                    params.m_outputBundlePath.AbsolutePath(),
                    params.m_bundleVersion,
                    params.m_maxBundleSizeInMB);
                if (!overrideOutcome.IsSuccess())
                {
                    // Metric event has already been sent
                    AZ_Error(AppWindowName, false, overrideOutcome.GetError().c_str());
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }

                FilePath bundleFilePath(bundleSettings.first.m_bundleFilePath);

                // Check if we are performing a destructive overwrite that the user did not approve 
                if (!params.m_allowOverwrites && AZ::IO::FileIOBase::GetInstance()->Exists(bundleFilePath.AbsolutePath().c_str()))
                {
                    SendErrorMetricEvent("Unapproved destructive overwrite on a Bundle.");
                    AZ_Error(AssetBundler::AppWindowName, false, "Bundle ( %s ) already exists, running this command would perform a destructive overwrite.\n\n"
                        "Run your command again with the ( --%s ) arg if you want to save over the existing file.", bundleFilePath.AbsolutePath().c_str(), AllowOverwritesFlag);
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }

                AZ_TracePrintf(AssetBundler::AppWindowName, "Creating Bundle ( %s )...\n", bundleFilePath.AbsolutePath().c_str());
                bool result = false;
                AssetBundleCommandsBus::BroadcastResult(result, &AssetBundleCommandsBus::Events::CreateAssetBundle, bundleSettings.first);
                if (!result)
                {
                    SendErrorMetricEvent("Unable to create bundle.");
                    AZ_Error(AssetBundler::AppWindowName, false, "Unable to create bundle, target Bundle file path is ( %s ).", bundleFilePath.AbsolutePath().c_str());
                    failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                    return;
                }
                AZ_TracePrintf(AssetBundler::AppWindowName, "Bundle ( %s ) created successfully!\n", bundleFilePath.AbsolutePath().c_str());
            });

        return failureCount == 0;
    }

    bool ApplicationManager::RunBundleSeedCommands(const AZ::Outcome<BundleSeedParams, AZStd::string>& paramsOutcome)
    {
        using namespace AzToolsFramework;

        if (!paramsOutcome.IsSuccess())
        {
            SendErrorMetricEvent(paramsOutcome.GetError().c_str());
            AZ_Error(AppWindowName, false, paramsOutcome.GetError().c_str());
            return false;
        }

        BundleSeedParams params = paramsOutcome.GetValue();

        // If no platform was input we want to loop over all possible platforms and make bundles for whatever we find
        if (params.m_bundleParams.m_platformFlags == AzFramework::PlatformFlags::Platform_NONE)
        {
            params.m_bundleParams.m_platformFlags = AzFramework::PlatformFlags::AllNamedPlatforms;
        }

        AZStd::vector<AssetBundleSettings> allBundleSettings;
        if (params.m_bundleParams.m_bundleSettingsFile.AbsolutePath().empty())
        {
            // if no bundle settings file was provided generate one for each platform, values will be overridden later
            for (AZStd::string& platformName : AzFramework::PlatformHelper::GetPlatformsInterpreted(params.m_bundleParams.m_platformFlags))
            {
                allBundleSettings.emplace_back();
                allBundleSettings.back().m_platform = platformName;
            }
            
        }
        else
        {
            // if a bundle settings file was provided use values from the file, leave the asset list file path behind since it will be ignored anyways
            AZStd::vector<FilePath> allBundleSettingsFilePaths = GetAllPlatformSpecificFilesOnDisk(params.m_bundleParams.m_bundleSettingsFile, params.m_bundleParams.m_platformFlags);

            // Attempt to load all Bundle Settings files (there may be one or many to load)
            for (const auto& bundleSettingsFilePath : allBundleSettingsFilePaths)
            {
                AZ::Outcome<AssetBundleSettings, AZStd::string> loadBundleSettingsOutcome = AssetBundleSettings::Load(bundleSettingsFilePath.AbsolutePath());
                if (!loadBundleSettingsOutcome.IsSuccess())
                {
                    SendErrorMetricEvent("Failed to load Bundle Settings file.");
                    AZ_Error(AssetBundler::AppWindowName, false, loadBundleSettingsOutcome.GetError().c_str());
                    return false;
                }
                allBundleSettings.emplace_back(loadBundleSettingsOutcome.TakeValue());
            }
        }

        // Create all Bundles
        for (AssetBundleSettings& bundleSettings : allBundleSettings)
        {
            auto overrideOutcome = ApplyBundleSettingsOverrides(
                bundleSettings,
                params.m_bundleParams.m_assetListFile.AbsolutePath(),
                params.m_bundleParams.m_outputBundlePath.AbsolutePath(),
                params.m_bundleParams.m_bundleVersion,
                params.m_bundleParams.m_maxBundleSizeInMB);

            if (!overrideOutcome.IsSuccess())
            {
                // Metric event has already been sent
                AZ_Error(AppWindowName, false, overrideOutcome.GetError().c_str());
                return false;
            }

            if (!params.m_bundleParams.m_allowOverwrites && AZ::IO::FileIOBase::GetInstance()->Exists(bundleSettings.m_bundleFilePath.c_str()))
            {
                SendErrorMetricEvent("Unapproved destructive overwrite on a Bundle.");
                AZ_Error(AssetBundler::AppWindowName, false, "Bundle ( %s ) already exists, running this command would perform a destructive overwrite.\n\n"
                    "Run your command again with the ( --%s ) arg if you want to save over the existing file.", bundleSettings.m_bundleFilePath.c_str(), AllowOverwritesFlag);
                return false;
            }

            AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlag(bundleSettings.m_platform);
            AzFramework::PlatformId platformId = static_cast<AzFramework::PlatformId>(AzFramework::PlatformHelper::GetPlatformIndexFromName(bundleSettings.m_platform.c_str()));

            for (const AZStd::string& assetPath : params.m_addSeedList)
            {
                m_assetSeedManager->AddSeedAsset(assetPath, platformFlag);
            }

            auto assetList = m_assetSeedManager->GetDependenciesInfo(platformId, {});
            if (assetList.size() == 0)
            {
                AZ_TracePrintf(AssetBundler::AppWindowName, "Platform ( %s ) had no assets based on these seeds, skipping bundle generation.\n", bundleSettings.m_platform.c_str());
            }
            else
            {
                AssetFileInfoList assetFileInfoList;
                // convert from AZ::Data::AssetInfo to AssetFileInfo for AssetBundleAPI call
                for (const auto& asset : assetList)
                {
                    AssetFileInfo assetInfo;
                    assetInfo.m_assetId = asset.m_assetId;
                    assetInfo.m_assetRelativePath = asset.m_relativePath;
                    assetFileInfoList.m_fileInfoList.emplace_back(assetInfo);
                }

                AZ_TracePrintf(AssetBundler::AppWindowName, "Creating Bundle ( %s )...\n", bundleSettings.m_bundleFilePath.c_str());
                bool result = false;
                AssetBundleCommandsBus::BroadcastResult(result, &AssetBundleCommandsBus::Events::CreateAssetBundleFromList, bundleSettings, assetFileInfoList);
                if (!result)
                {
                    SendErrorMetricEvent("Unable to create bundle.");
                    AZ_Error(AssetBundler::AppWindowName, false, "Unable to create bundle, target Bundle file path is ( %s ).", bundleSettings.m_bundleFilePath.c_str());
                    return false;
                }
                AZ_TracePrintf(AssetBundler::AppWindowName, "Bundle ( %s ) created successfully!\n", bundleSettings.m_bundleFilePath.c_str());
            }
        }

        return true;
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::InitAssetCatalog(AzFramework::PlatformFlags platforms, const AZStd::string& assetCatalogFile)
    {
        using namespace AzToolsFramework;
        if (platforms == AzFramework::PlatformFlags::Platform_NONE)
        {
            return AZ::Failure(AZStd::string("Invalid platform.\n"));
        }

        for (const AzFramework::PlatformId& platformId : AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platforms))
        {
            AZStd::string platformSpecificAssetCatalogPath;
            if (assetCatalogFile.empty())
            {
                AzFramework::StringFunc::Path::ConstructFull(
                    PlatformAddressedAssetCatalog::GetAssetRootForPlatform(platformId).c_str(),
                    AssetBundler::AssetCatalogFilename,
                    platformSpecificAssetCatalogPath);
            }
            else
            {
                platformSpecificAssetCatalogPath = assetCatalogFile;
            }

            AZ_TracePrintf(AssetBundler::AppWindowNameVerbose, "Loading asset catalog from ( %s ).\n", platformSpecificAssetCatalogPath.c_str());

            bool success = false;
            {
                AzToolsFramework::AssetCatalog::PlatformAddressedAssetCatalogRequestBus::EventResult(success, platformId, &AzToolsFramework::AssetCatalog::PlatformAddressedAssetCatalogRequestBus::Events::LoadCatalog, platformSpecificAssetCatalogPath.c_str());
            }
            if (!success && !AzFramework::PlatformHelper::IsSpecialPlatform(platforms))
            {
                SendErrorMetricEvent("Failed to open asset catalog file.");
                return AZ::Failure(AZStd::string::format("Failed to open asset catalog file ( %s ).", platformSpecificAssetCatalogPath.c_str()));
            }
        }

        return AZ::Success();
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::LoadSeedListFile(const AZStd::string& seedListFileAbsolutePath, AzFramework::PlatformFlags platformFlags)
    {
        AZ::Outcome<void, AZStd::string> fileExtensionOutcome = AzToolsFramework::AssetSeedManager::ValidateSeedFileExtension(seedListFileAbsolutePath);
        if (!fileExtensionOutcome.IsSuccess())
        {
            return fileExtensionOutcome;
        }

        bool seedListFileExists = AZ::IO::FileIOBase::GetInstance()->Exists(seedListFileAbsolutePath.c_str());

        if (seedListFileExists)
        {
            AZ_TracePrintf(AssetBundler::AppWindowName, "Loading Seed List file ( %s ).\n", seedListFileAbsolutePath.c_str());

            if (!IsGemSeedFilePathValid(GetEngineRoot(), seedListFileAbsolutePath, m_gemInfoList, platformFlags))
            {
                SendErrorMetricEvent("Gem Seed File Path is not valid.");
                return AZ::Failure(AZStd::string::format(
                    "Invalid Seed List file ( %s ). This can happen if you add a seed file from a gem that is not enabled for the current project ( %s ).",
                    seedListFileAbsolutePath.c_str(),
                    m_currentProjectName.c_str()));
            }

            if (!m_assetSeedManager->Load(seedListFileAbsolutePath))
            {
                SendErrorMetricEvent("Failed to load Seed List file.");
                return AZ::Failure(AZStd::string::format("Failed to load Seed List file ( %s ).", seedListFileAbsolutePath.c_str()));
            }
        }

        return AZ::Success();
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::LoadProjectDependenciesFile(AzFramework::PlatformFlags platformFlags)
    {
        AZStd::string projectDependenciesFile = AZStd::move(GetProjectDependenciesFile(g_cachedEngineRoot));
        if (!AZ::IO::FileIOBase::GetInstance()->Exists(projectDependenciesFile.c_str()))
        {
            AZ_TracePrintf(AssetBundler::AppWindowName, "Project dependencies file %s doesn't exist.\n", projectDependenciesFile.c_str());

            AZStd::string projectDependenciesFileTemplate = AZStd::move(GetProjectDependenciesFileTemplate(g_cachedEngineRoot));
            if (AZ::IO::FileIOBase::GetInstance()->Copy(projectDependenciesFileTemplate.c_str(), projectDependenciesFile.c_str()))
            {
                AZ_TracePrintf(AssetBundler::AppWindowName, "Copied project dependencies file template %s to the current project.\n",
                    projectDependenciesFile.c_str());
            }
            else
            {
                return AZ::Failure(AZStd::string::format("Failed to copy project dependencies file template %s from default project"
                    " template to the current project.\n", projectDependenciesFileTemplate.c_str()));
            }
        }

        // Add the project dependencies file to the seed list
        AZStd::string relativeProductPath;
        AzFramework::StringFunc::Path::GetFullFileName(projectDependenciesFile.c_str(), relativeProductPath);
        AZStd::to_lower(relativeProductPath.begin(), relativeProductPath.end());
        m_assetSeedManager->AddSeedAsset(relativeProductPath, platformFlags);

        return AZ::Success();
    }

    void ApplicationManager::PrintSeedList(const AZStd::string& seedListFileAbsolutePath)
    {
        AZ_Printf(AppWindowName, "\nContents of ( %s ):\n\n", seedListFileAbsolutePath.c_str());
        for (const AzFramework::SeedInfo& seed : m_assetSeedManager->GetAssetSeedList())
        {
            AZ_Printf(AppWindowName, "%-60s%s\n", seed.m_assetRelativePath.c_str(), m_assetSeedManager->GetReadablePlatformList(seed).c_str());
        }
        AZ_Printf(AppWindowName, "\n");
    }

    bool ApplicationManager::RunPlatformSpecificAssetListCommands(const AssetListsParams& params, AzFramework::PlatformFlags platformFlags)
    {
        using namespace AzToolsFramework;
        AZStd::vector<AzFramework::PlatformId> platformIds = AzFramework::PlatformHelper::GetPlatformIndices(platformFlags);
        AZStd::vector<AzFramework::PlatformId> platformIdsInterpreted = AzFramework::PlatformHelper::GetPlatformIndicesInterpreted(platformFlags);

        // Add Seeds
        for (const auto& platformId : platformIds)
        {
            AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);

            for (const AZStd::string& assetPath : params.m_addSeedList)
            {
                m_assetSeedManager->AddSeedAsset(assetPath, platformFlag);
            }
        }

        AZStd::unordered_set<AZ::Data::AssetId> exclusionList;

        for (const AZStd::string& asset : params.m_skipList)
        {
            AZ::Data::AssetId assetId = m_assetSeedManager->GetAssetIdByPath(asset, platformFlags);

            if (assetId.IsValid())
            {
                exclusionList.emplace(assetId);
            }
        }

        // Print
        bool printExistingFiles = false;
        if (params.m_print)
        {
            printExistingFiles = !params.m_assetListFile.AbsolutePath().empty()
                && params.m_seedListFiles.empty()
                && params.m_addSeedList.empty()
                && !params.m_addDefaultSeedListFiles;
            PrintAssetLists(params, platformIdsInterpreted, printExistingFiles, exclusionList);
        }

        // Dry Run
        if (params.m_dryRun || params.m_assetListFile.AbsolutePath().empty() || printExistingFiles)
        {
            return true;
        }

        AZ_Printf(AssetBundler::AppWindowName, "\n");

        AZStd::atomic_uint failureCount = 0;

        // Save 
        AZ::parallel_for_each(platformIdsInterpreted.begin(), platformIdsInterpreted.end(), [this, &params, &failureCount, &exclusionList](AzFramework::PlatformId platformId)
        {
            AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);

            FilePath platformSpecificAssetListFilePath = FilePath(params.m_assetListFile.AbsolutePath(), AzFramework::PlatformHelper::GetPlatformName(platformId));
            AZStd::string assetListFileAbsolutePath = platformSpecificAssetListFilePath.AbsolutePath();

            AZ_TracePrintf(AssetBundler::AppWindowName, "Saving Asset List file to ( %s )...\n", assetListFileAbsolutePath.c_str());

            // Check if we are performing a destructive overwrite that the user did not approve 
            if (!params.m_allowOverwrites && AZ::IO::FileIOBase::GetInstance()->Exists(assetListFileAbsolutePath.c_str()))
            {
                SendErrorMetricEvent("Unapproved destructive overwrite on an Asset List file.");
                AZ_Error(AssetBundler::AppWindowName, false, "Asset List file ( %s ) already exists, running this command would perform a destructive overwrite.\n\n"
                    "Run your command again with the ( --%s ) arg if you want to save over the existing file.\n", assetListFileAbsolutePath.c_str(), AllowOverwritesFlag);
                failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                return;
            }

            // Generate Debug file
            AZStd::string debugListFileAbsolutePath;
            if (params.m_generateDebugFile)
            {
                debugListFileAbsolutePath = assetListFileAbsolutePath;
                AzFramework::StringFunc::Path::ReplaceExtension(debugListFileAbsolutePath, AssetFileDebugInfoList::GetAssetListDebugFileExtension());
                AZ_TracePrintf(AssetBundler::AppWindowName, "Saving Asset List Debug file to ( %s )...\n", debugListFileAbsolutePath.c_str());
            }

            if (!m_assetSeedManager->SaveAssetFileInfo(assetListFileAbsolutePath, platformFlag, exclusionList, debugListFileAbsolutePath))
            {
                SendErrorMetricEvent("Failed to save Asset List file.");
                AZ_Error(AssetBundler::AppWindowName, false, "Unable to save Asset List file to ( %s ).\n", assetListFileAbsolutePath.c_str());
                failureCount.fetch_add(1, AZStd::memory_order::memory_order_relaxed);
                return;
            }

            AZ_TracePrintf(AssetBundler::AppWindowName, "Save successful! ( %s )\n", assetListFileAbsolutePath.c_str());
        });

        return failureCount == 0;
    }

    void ApplicationManager::PrintAssetLists(const AssetListsParams& params, const AZStd::vector<AzFramework::PlatformId>& platformIds, bool printExistingFiles,const AZStd::unordered_set<AZ::Data::AssetId>& exclusionList)
    {
        using namespace AzToolsFramework;

        // The user wants to print the contents of a pre-existing Asset List file *without* modifying it
        if (printExistingFiles)
        {
            AZStd::vector<FilePath> allAssetListFiles = GetAllPlatformSpecificFilesOnDisk(params.m_assetListFile, params.m_platformFlags);

            for (const FilePath& assetListFilePath : allAssetListFiles)
            {
                auto assetFileInfoOutcome = m_assetSeedManager->LoadAssetFileInfo(assetListFilePath.AbsolutePath());
                if (!assetFileInfoOutcome.IsSuccess())
                {
                    AZ_Error(AssetBundler::AppWindowName, false, assetFileInfoOutcome.GetError().c_str());
                }

                AZ_Printf(AssetBundler::AppWindowName, "\nPrinting contents of ( %s ):\n", assetListFilePath.AbsolutePath().c_str());

                for (const AssetFileInfo& assetFileInfo : assetFileInfoOutcome.GetValue().m_fileInfoList)
                {
                    AZ_Printf(AssetBundler::AppWindowName, "- %s\n", assetFileInfo.m_assetRelativePath.c_str());
                }

                AZ_Printf(AssetBundler::AppWindowName, "Total number of assets in ( %s ): %d\n", assetListFilePath.AbsolutePath().c_str(), assetFileInfoOutcome.GetValue().m_fileInfoList.size());
            }
            return;
        }

        // The user wants to print the contents of a recently-modified Asset List file
        for (const AzFramework::PlatformId platformId : platformIds)
        {
            AzFramework::PlatformFlags platformFlag = AzFramework::PlatformHelper::GetPlatformFlagFromPlatformIndex(platformId);

            AssetSeedManager::AssetsInfoList assetsInfoList = m_assetSeedManager->GetDependenciesInfo(platformId, exclusionList);

            AZ_Printf(AssetBundler::AppWindowName, "\nPrinting assets for Platform ( %s ):\n", AzFramework::PlatformHelper::GetPlatformName(platformId));

            for (const AZ::Data::AssetInfo& assetInfo : assetsInfoList)
            {
                AZ_Printf(AssetBundler::AppWindowName, "- %s\n", assetInfo.m_relativePath.c_str());
            }

            AZ_Printf(AssetBundler::AppWindowName, "Total number of assets for Platform ( %s ): %d.\n", AzFramework::PlatformHelper::GetPlatformName(platformId), assetsInfoList.size());
        }
    }

    AZStd::vector<FilePath> ApplicationManager::GetAllPlatformSpecificFilesOnDisk(const FilePath& platformIndependentFilePath, AzFramework::PlatformFlags platformFlags)
    {
        using namespace AzToolsFramework;
        AZStd::vector<FilePath> platformSpecificPaths;

        if (platformIndependentFilePath.AbsolutePath().empty())
        {
            return platformSpecificPaths;
        }

        FilePath testFilePath;

        for (const AZStd::string& platformName : AzFramework::PlatformHelper::GetPlatformsInterpreted(platformFlags))
        {
            testFilePath = FilePath(platformIndependentFilePath.AbsolutePath(), platformName);
            if (!testFilePath.AbsolutePath().empty() && AZ::IO::FileIOBase::GetInstance()->Exists(testFilePath.AbsolutePath().c_str()))
            {
                platformSpecificPaths.emplace_back(testFilePath.AbsolutePath());
            }
        }

        return platformSpecificPaths;
    }

    AZ::Outcome<void, AZStd::string> ApplicationManager::ApplyBundleSettingsOverrides(
        AzToolsFramework::AssetBundleSettings& bundleSettings, 
        const AZStd::string& assetListFilePath, 
        const AZStd::string& outputBundleFilePath, 
        int bundleVersion, 
        int maxBundleSize)
    {
        using namespace AzToolsFramework;

        // Asset List file path
        if (!assetListFilePath.empty())
        {
            FilePath platformSpecificPath = FilePath(assetListFilePath, bundleSettings.m_platform);
            if (platformSpecificPath.AbsolutePath().empty())
            {
                SendErrorMetricEvent("Failed to apply Bundle Settings overrides (assetListFilePath)");
                return AZ::Failure(AZStd::string::format(
                    "Failed to apply Bundle Settings overrides: ( %s ) is incompatible with input Bundle Settings file.",
                    assetListFilePath.c_str()));
            }
            bundleSettings.m_assetFileInfoListPath = platformSpecificPath.AbsolutePath();
        }

        // Output Bundle file path
        if (!outputBundleFilePath.empty())
        {
            FilePath platformSpecificPath = FilePath(outputBundleFilePath, bundleSettings.m_platform);
            if (platformSpecificPath.AbsolutePath().empty())
            {
                SendErrorMetricEvent("Failed to apply Bundle Settings overrides (outputBundleFilePath)");
                return AZ::Failure(AZStd::string::format(
                    "Failed to apply Bundle Settings overrides: ( %s ) is incompatible with input Bundle Settings file.",
                    outputBundleFilePath.c_str()));
            }
            bundleSettings.m_bundleFilePath = platformSpecificPath.AbsolutePath();
        }

        // Bundle Version
        if (bundleVersion > 0 && bundleVersion <= AzFramework::AssetBundleManifest::CurrentBundleVersion)
        {
            bundleSettings.m_bundleVersion = bundleVersion;
        }

        // Max Bundle Size
        if (maxBundleSize > 0 && maxBundleSize <= AssetBundleSettings::GetMaxBundleSizeInMB())
        {
            bundleSettings.m_maxBundleSizeInMB = maxBundleSize;
        }

        return AZ::Success();
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Output Help Text
    ////////////////////////////////////////////////////////////////////////////////////////////

    void ApplicationManager::OutputHelp(CommandType commandType)
    {
        using namespace AssetBundler;

        AZ_Printf(AppWindowName, "This program can be used to create asset bundles that can be used by the runtime to load assets.\n");
        AZ_Printf(AppWindowName, "--%-20s-Displays more detailed output messages.\n\n", VerboseFlag);

        switch (commandType)
        {
        case CommandType::Seeds:
            OutputHelpSeeds();
            break;
        case CommandType::AssetLists:
            OutputHelpAssetLists();
            break;
        case CommandType::ComparisonRules:
            OutputHelpComparisonRules();
            break;
        case CommandType::Compare:
            OutputHelpCompare();
            break;
        case CommandType::BundleSettings:
            OutputHelpBundleSettings();
            break;
        case CommandType::Bundles:
            OutputHelpBundles();
            break;
        case CommandType::BundleSeed:
            OutputHelpBundleSeed();
            break;
        case CommandType::Invalid:

            AZ_Printf(AppWindowName, "Input to this command follows the format: [subCommandName] --exampleArgThatTakesInput exampleInput --exampleFlagThatTakesNoInput\n");
            AZ_Printf(AppWindowName, "    - Example: \"assetLists --assetListFile example.assetlist --addDefaultSeedListFiles --print\"\n");
            AZ_Printf(AppWindowName, "\n");
            AZ_Printf(AppWindowName, "Some args in this tool take paths as arguments, and there are two main types:\n");
            AZ_Printf(AppWindowName, "          \"path\" - This refers to an Engine-Root-Relative path.\n");
            AZ_Printf(AppWindowName, "                 - Example: \"C:\\Lumberyard\\dev\\SamplesProject\\test.txt\" can be represented as \"SamplesProject\\test.txt\".\n");
            AZ_Printf(AppWindowName, "    \"cache path\" - This refers to a Cache-Relative path.\n");
            AZ_Printf(AppWindowName, "                 - Example: \"C:\\Lumberyard\\dev\\Cache\\SamplesProject\\pc\\samplesproject\\animations\\skeletonlist.xml\" is represented as \"animations\\skeletonlist.xml\".\n");
            AZ_Printf(AppWindowName, "\n");

            OutputHelpSeeds();
            OutputHelpAssetLists();
            OutputHelpComparisonRules();
            OutputHelpCompare();
            OutputHelpBundleSettings();
            OutputHelpBundles();
            OutputHelpBundleSeed();

            AZ_Printf(AppWindowName, "\n\nTo see less Help text, type in a Sub-Command before requesting the Help text. For example: \"%s --%s\".\n", SeedsCommand, HelpFlag);

            break;
        }

        if (commandType != CommandType::Invalid)
        {
            AZ_Printf(AppWindowName, "\n\nTo see more Help text, type: \"--%s\" without any other input.\n", HelpFlag);
        }
    }

    void ApplicationManager::OutputHelpSeeds()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for performing operations on Seed List files.\n", SeedsCommand);
        AZ_Printf(AppWindowName, "    --%-25s-[Required] Specifies the Seed List file to operate on by path. Must include (.%s) file extension.\n", SeedListFileArg, AssetSeedManager::GetSeedFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Adds the asset to the list of root assets for the specified platform.\n", AddSeedArg);
        AZ_Printf(AppWindowName, "%-31s---Takes in a cache path to a pre-processed asset.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Removes the asset from the list of root assets for the specified platform.\n", RemoveSeedArg);
        AZ_Printf(AppWindowName, "%-31s---To completely remove the asset, it must be removed for all platforms.\n", "");
        AZ_Printf(AppWindowName, "%-31s---Takes in a cache path to a pre-processed asset. A cache path is a path relative to \"...\\dev\\Cache\\ProjectName\\platform\\projectname\\\"\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Adds the specified platform to every Seed in the Seed List file, if possible.\n", AddPlatformToAllSeedsFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Removes the specified platform from every Seed in the Seed List file, if possible.\n", RemovePlatformFromAllSeedsFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Outputs the contents of the Seed List file after performing any specified operations.\n", PrintFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the platform(s) referenced by all Seed operations.\n", PlatformArg);
        AZ_Printf(AppWindowName, "%-31s---Requires an existing cache of assets for the input platform(s).\n", "");
        AZ_Printf(AppWindowName, "%-31s---Defaults to all enabled platforms. Platforms can be changed by modifying AssetProcessorPlatformConfig.ini.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Updates the path hints stored in the Seed List file.\n", UpdateSeedPathArg);
        AZ_Printf(AppWindowName, "    --%-25s-Removes the path hints stored in the Seed List file.\n", RemoveSeedPathArg);
        AZ_Printf(AppWindowName, "    --%-25s-[Testing] Specifies the Asset Catalog file referenced by all Seed operations.\n", AssetCatalogFileArg);
        AZ_Printf(AppWindowName, "%-31s---Designed to be used in Unit Tests.\n", "");
    }

    void ApplicationManager::OutputHelpAssetLists()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for generating Asset List Files.\n", AssetListsCommand);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Asset List file to operate on by path. Must include (.%s) file extension.\n", AssetListFileArg, AssetSeedManager::GetAssetListFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Seed List file(s) that will be used as root(s) when generating this Asset List file.\n", SeedListFileArg);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Seed(s) to use as root(s) when generating this Asset List File.\n", AddSeedArg);
        AZ_Printf(AppWindowName, "%-31s---Takes in a cache path to a pre-processed asset. A cache path is a path relative to \"...\\dev\\Cache\\ProjectName\\platform\\projectname\\\"\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-The specified files and all dependencies will be ignored when generating the Asset List file.\n", SkipArg);
        AZ_Printf(AppWindowName, "%-31s---Takes in a comma-separated list of cache paths to pre-processed assets.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Automatically include all default Seed List files in generated Asset List File.\n", AddDefaultSeedListFilesFlag);
        AZ_Printf(AppWindowName, "%-31s---This will include Seed List files for the Lumberyard Engine and all enabled Gems.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the platform(s) to generate an Asset List file for.\n", PlatformArg);
        AZ_Printf(AppWindowName, "%-31s---Requires an existing cache of assets for the input platform(s).\n", "");
        AZ_Printf(AppWindowName, "%-31s---Defaults to all enabled platforms. Platforms can be changed by modifying AssetProcessorPlatformConfig.ini.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-[Testing] Specifies the Asset Catalog file referenced by all Asset List operations.\n", AssetCatalogFileArg);
        AZ_Printf(AppWindowName, "%-31s---Designed to be used in Unit Tests.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Outputs the contents of the Asset List file after adding any specified seed files.\n", PrintFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Run all input commands, without saving to the specified Asset List file.\n", DryRunFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Generates a human-readable file that maps every entry in the Asset List file to the Seed that generated it.\n", GenerateDebugFileFlag);
        AZ_Printf(AppWindowName, "    --%-25s-Allow destructive overwrites of files. Include this arg in automation.\n", AllowOverwritesFlag);
    }

    void ApplicationManager::OutputHelpComparisonRules()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for generating Comparison Rules files.\n", ComparisonRulesCommand);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Comparison Rules file to operate on by path.\n", ComparisonRulesFileArg);
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of Comparison types.\n", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "%-31s---Valid inputs: 0 (Delta), 1 (Union), 2 (Intersection), 3 (Complement), 4 (FilePattern).\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of file pattern matching types.\n", ComparisonFilePatternTypeArg);
        AZ_Printf(AppWindowName, "%-31s---Valid inputs: 0 (Wildcard), 1 (Regex).\n", "");
        AZ_Printf(AppWindowName, "%-31s---Must match the number of FilePattern comparisons specified in ( --%s ) argument list.\n", "", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of file patterns.\n", ComparisonFilePatternArg);
        AZ_Printf(AppWindowName, "%-31s---Must match the number of FilePattern comparisons specified in ( --%s ) argument list.\n", "", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "    --%-25s-Allow destructive overwrites of files. Include this arg in automation.\n", AllowOverwritesFlag);
    }

    void ApplicationManager::OutputHelpCompare()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for performing comparisons between asset list files.\n", CompareCommand);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Comparison Rules file to load rules from.\n", ComparisonRulesFileArg);
        AZ_Printf(AppWindowName, "%-31s---All additional comparison rules specified in this command will be done after the comparison operations loaded from the rules file.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of comparison types.\n", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "%-31s---Valid inputs: 0 (Delta), 1 (Union), 2 (Intersection), 3 (Complement), 4 (FilePattern), 5 (IntersectionCount).\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of file pattern matching types.\n", ComparisonFilePatternTypeArg);
        AZ_Printf(AppWindowName, "%-31s---Valid inputs: 0 (Wildcard), 1 (Regex).\n", "");
        AZ_Printf(AppWindowName, "%-31s---Must match the number of FilePattern comparisons specified in ( --%s ) argument list.\n", "", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of file patterns.\n", ComparisonFilePatternArg);
        AZ_Printf(AppWindowName, "%-31s---Must match the number of FilePattern comparisons specified in ( --%s ) argument list.\n", "", ComparisonTypeArg);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the count that will be used during the %s compare operation.\n", IntersectionCountArg, AssetFileInfoListComparison::ComparisonTypeNames[aznumeric_cast<AZ::u8>(AssetFileInfoListComparison::ComparisonType::IntersectionCount)]);
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of first inputs for comparison.\n", CompareFirstFileArg);
        AZ_Printf(AppWindowName, "%-31s---Must match the number of comparison operations.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of second inputs for comparison.\n", CompareSecondFileArg);
        AZ_Printf(AppWindowName, "%-31s---Must match the number of comparison operations that require two inputs.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of outputs for the comparison command.\n", CompareOutputFileArg);
        AZ_Printf(AppWindowName, "%-31s---Must match the number of comparison operations.\n", "");
        AZ_Printf(AppWindowName, "%-31s---Inputs and outputs can be a file or a variable passed from another comparison.\n", "");
        AZ_Printf(AppWindowName, "%-31s---Variables are specified by the prefix %c.\n", "", compareVariablePrefix);
        AZ_Printf(AppWindowName, "    --%-25s-A comma seperated list of paths or variables to print to console after comparison operations complete.\n", ComparePrintArg);
        AZ_Printf(AppWindowName, "%-31s---Leave list blank to just print the final comparison result.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Allow destructive overwrites of files. Include this arg in automation.\n", AllowOverwritesFlag);
    }

    void ApplicationManager::OutputHelpBundleSettings()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for performing operations on Bundle Settings files.\n", BundleSettingsCommand);
        AZ_Printf(AppWindowName, "    --%-25s-[Required] Specifies the Bundle Settings file to operate on by path. Must include (.%s) file extension.\n", BundleSettingsFileArg, AssetBundleSettings::GetBundleSettingsFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Sets the Asset List file to use for Bundle generation. Must include (.%s) file extension.\n", AssetListFileArg, AssetSeedManager::GetAssetListFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Sets the path where generated Bundles will be stored. Must include (.%s) file extension.\n", OutputBundlePathArg, AssetBundleSettings::GetBundleFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Determines which version of Lumberyard Bundles to generate. Current version is (%i).\n", BundleVersionArg, AzFramework::AssetBundleManifest::CurrentBundleVersion);
        AZ_Printf(AppWindowName, "    --%-25s-Sets the maximum size for a single Bundle (in MB). Default size is (%i MB).\n", MaxBundleSizeArg, AssetBundleSettings::GetMaxBundleSizeInMB());
        AZ_Printf(AppWindowName, "%-31s---Bundles larger than this limit will be divided into a series of smaller Bundles and named accordingly.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the platform(s) referenced by all Bundle Settings operations.\n", PlatformArg);
        AZ_Printf(AppWindowName, "%-31s---Defaults to all enabled platforms. Platforms can be changed by modifying AssetProcessorPlatformConfig.ini.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Outputs the contents of the Bundle Settings file after modifying any specified values.\n", PrintFlag);
    }

    void ApplicationManager::OutputHelpBundles()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for generating bundles. Must provide either (--%s) or (--%s and --%s).\n", BundlesCommand, BundleSettingsFileArg, AssetListFileArg, OutputBundlePathArg);
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Bundle Settings files to operate on by path. Must include (.%s) file extension.\n", BundleSettingsFileArg, AssetBundleSettings::GetBundleSettingsFileExtension());
        AZ_Printf(AppWindowName, "%-31s---If any other args are specified, they will override the values stored inside this file.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Sets the Asset List files to use for Bundle generation. Must include (.%s) file extension.\n", AssetListFileArg, AssetSeedManager::GetAssetListFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Sets the paths where generated Bundles will be stored. Must include (.%s) file extension.\n", OutputBundlePathArg, AssetBundleSettings::GetBundleFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Determines which versions of Lumberyard Bundles to generate. Current version is (%i).\n", BundleVersionArg, AzFramework::AssetBundleManifest::CurrentBundleVersion);
        AZ_Printf(AppWindowName, "    --%-25s-Sets the maximum size for Bundles (in MB). Default size is (%i MB).\n", MaxBundleSizeArg, AssetBundleSettings::GetMaxBundleSizeInMB());
        AZ_Printf(AppWindowName, "%-31s---Bundles larger than this limit will be divided into a series of smaller Bundles and named accordingly.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the platform(s) that will be referenced when generating Bundles.\n", PlatformArg);
        AZ_Printf(AppWindowName, "%-31s---If no platforms are specified, Bundles will be generated for all available platforms.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Allow destructive overwrites of files. Include this arg in automation.\n", AllowOverwritesFlag);
    }

    void ApplicationManager::OutputHelpBundleSeed()
    {
        using namespace AzToolsFramework;
        AZ_Printf(AppWindowName, "\n%-25s-Subcommand for generating bundles directly from seeds. Must provide either (--%s) or (--%s).\n", BundleSeedCommand, BundleSettingsFileArg, OutputBundlePathArg);
        AZ_Printf(AppWindowName, "    --%-25s-Adds the asset to the list of root assets for the specified platform.\n", AddSeedArg);
        AZ_Printf(AppWindowName, "%-31s---Takes in a cache path to a pre-processed asset. A cache path is a path relative to \"...\\dev\\Cache\\ProjectName\\platform\\projectname\\\"\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the Bundle Settings file to operate on by path. Must include (.%s) file extension.\n", BundleSettingsFileArg, AssetBundleSettings::GetBundleSettingsFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Sets the path where generated Bundles will be stored. Must include (.%s) file extension.\n", OutputBundlePathArg, AssetBundleSettings::GetBundleFileExtension());
        AZ_Printf(AppWindowName, "    --%-25s-Determines which version of Lumberyard Bundles to generate. Current version is (%i).\n", BundleVersionArg, AzFramework::AssetBundleManifest::CurrentBundleVersion);
        AZ_Printf(AppWindowName, "    --%-25s-Sets the maximum size for a single Bundle (in MB). Default size is (%i MB).\n", MaxBundleSizeArg, AssetBundleSettings::GetMaxBundleSizeInMB());
        AZ_Printf(AppWindowName, "%-31s---Bundles larger than this limit will be divided into a series of smaller Bundles and named accordingly.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Specifies the platform(s) that will be referenced when generating Bundles.\n", PlatformArg);
        AZ_Printf(AppWindowName, "%-31s---If no platforms are specified, Bundles will be generated for all available platforms.\n", "");
        AZ_Printf(AppWindowName, "    --%-25s-Allow destructive overwrites of files. Include this arg in automation.\n", AllowOverwritesFlag);
        AZ_Printf(AppWindowName, "    --%-25s-[Testing] Specifies the Asset Catalog file referenced by all Bundle operations.\n", AssetCatalogFileArg);
        AZ_Printf(AppWindowName, "%-31s---Designed to be used in Unit Tests.\n", "");
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Formatting for Output Text
    ////////////////////////////////////////////////////////////////////////////////////////////

    bool ApplicationManager::OnPreError(const char* window, const char* fileName, int line, const char* func, const char* message)
    {
        printf("\n");
        printf("[ERROR] - %s:\n", window);

        if (m_showVerboseOutput)
        {
            printf("(%s - Line %i)\n", fileName, line);
        }

        printf("%s", message);
        printf("\n");
        return true;
    }

    bool ApplicationManager::OnPreWarning(const char* window, const char* fileName, int line, const char* func, const char* message)
    {
        printf("\n");
        printf("[WARN] - %s:\n", window);

        if (m_showVerboseOutput)
        {
            printf("(%s - Line %i)\n", fileName, line);
        }

        printf("%s", message);
        printf("\n");
        return true;
    }

    bool ApplicationManager::OnPrintf(const char* window, const char* message)
    {
        if (window == AssetBundler::AppWindowName || (m_showVerboseOutput && window == AssetBundler::AppWindowNameVerbose))
        {
            printf("%s", message);
            return true;
        }

        return !m_showVerboseOutput;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    // Metrics
    ////////////////////////////////////////////////////////////////////////////////////////////

    void ApplicationManager::InitMetrics()
    {
        static const uint32_t processIntervalInSeconds = 2;
        LyMetrics_Initialize(
            "AssetBundler",
            processIntervalInSeconds,
            true,
            nullptr,
            nullptr,
            LY_METRICS_BUILD_TIME);
        m_assetBundlerMetricId = LyMetrics_CreateEvent("assetBundlerRunEvent");
    }

    void ApplicationManager::ShutDownMetrics()
    {
        LyMetrics_Shutdown();
    }

    void ApplicationManager::SendErrorMetricEvent(const char *errorMessage) const
    {
        LyMetricIdType metricEventId = LyMetrics_CreateEvent("assetBundlerErrorEvent");
        LyMetrics_AddAttribute(metricEventId, "errorMessage", errorMessage);
        LyMetrics_SubmitEvent(metricEventId);
    }

    void ApplicationManager::AddFlagAttribute(const char* key, bool flagValue) const
    {
        LyMetrics_AddAttribute(m_assetBundlerMetricId, key, flagValue ? "set" : "clear");
    }

    void ApplicationManager::AddMetric(const char* metricName, double metricValue) const
    {
        LyMetrics_AddMetric(m_assetBundlerMetricId, metricName, metricValue);
    }

} // namespace AssetBundler
