// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library.Helpers;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class MagnifierSettings : BasePTModuleSettings, ISettingsConfig, IHotkeyConfig
    {
        public const string ModuleName = "Magnifier";

        [JsonPropertyName("properties")]
        public MagnifierProperties Properties { get; set; }

        public MagnifierSettings()
        {
            Logger.LogInfo($"[MagnifierSettings] Constructor called - starting initialization");

            try
            {
                Logger.LogInfo($"[MagnifierSettings] Setting Name to ModuleName: '{ModuleName}'");
                Name = ModuleName;
                Logger.LogInfo($"[MagnifierSettings] Name set successfully to: '{Name}'");

                Logger.LogInfo($"[MagnifierSettings] Creating MagnifierProperties instance");
                Properties = new MagnifierProperties();
                Logger.LogInfo($"[MagnifierSettings] MagnifierProperties created successfully");

                Logger.LogInfo($"[MagnifierSettings] Setting Version to '1.0'");
                Version = "1.0";
                Logger.LogInfo($"[MagnifierSettings] Version set successfully to: '{Version}'");

                Logger.LogInfo($"[MagnifierSettings] Constructor completed successfully");
            }
            catch (Exception ex)
            {
                Logger.LogError($"[MagnifierSettings] EXCEPTION in constructor: {ex.Message}");
                Logger.LogError($"[MagnifierSettings] Full exception: {ex}");
                Logger.LogError($"[MagnifierSettings] Stack trace: {ex.StackTrace}");
                throw;
            }
        }

        public string GetModuleName()
        {
            return Name;
        }

        public ModuleType GetModuleType() => ModuleType.Magnifier;

        public HotkeyAccessor[] GetAllHotkeyAccessors()
        {
            var hotkeyAccessors = new List<HotkeyAccessor>
            {
                new HotkeyAccessor(
                    () => Properties.ActivationShortcut,
                    value => Properties.ActivationShortcut = value ?? Properties.DefaultActivationShortcut,
                    "MouseUtils_Magnifier_ActivationShortcut"),
            };

            return hotkeyAccessors.ToArray();
        }

        // This can be utilized in the future if the settings.json file is to be modified/deleted.
        public bool UpgradeSettingsConfiguration()
        {
            return false;
        }
    }
}
