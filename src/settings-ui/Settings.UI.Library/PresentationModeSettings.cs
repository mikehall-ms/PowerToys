// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Reflection;
using System.Text.Json.Serialization;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class PresentationModeSettings : BasePTModuleSettings, ISettingsConfig
    {
        public const string ModuleName = "PresentationMode";

        public PresentationModeSettings()
        {
            Name = ModuleName;
            Version = Assembly.GetExecutingAssembly().GetName().Version.ToString();
            Properties = new PresentationModeProperties();
        }

        [JsonPropertyName("properties")]
        public PresentationModeProperties Properties { get; set; }

        public string GetModuleName() => Name;

        public bool UpgradeSettingsConfiguration() => false;

        public ModuleType GetModuleType() => ModuleType.PresentationMode;
    }
}
