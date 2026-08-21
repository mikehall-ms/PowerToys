// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Collections.Generic;
using System.Text.Json.Serialization;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library.Helpers;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class RightClickLockSettings : BasePTModuleSettings, ISettingsConfig, IHotkeyConfig
    {
        public const string ModuleName = "RightClickLock";

        [JsonPropertyName("properties")]
        public RightClickLockProperties Properties { get; set; }

        public RightClickLockSettings()
        {
            Name = ModuleName;
            Properties = new RightClickLockProperties();
            Version = "1.0";
        }

        public string GetModuleName()
        {
            return Name;
        }

        public ModuleType GetModuleType() => ModuleType.RightClickLock;

        public HotkeyAccessor[] GetAllHotkeyAccessors()
        {
            var hotkeyAccessors = new List<HotkeyAccessor>
            {
                new HotkeyAccessor(
                    () => Properties.ActivationShortcut,
                    value => Properties.ActivationShortcut = value ?? Properties.DefaultActivationShortcut,
                    "MouseUtils_RightClickLock_ActivationShortcut"),
                new HotkeyAccessor(
                    () => Properties.PanicShortcut,
                    value => Properties.PanicShortcut = value ?? Properties.DefaultPanicShortcut,
                    "MouseUtils_RightClickLock_PanicShortcut"),
            };

            return hotkeyAccessors.ToArray();
        }

        public bool UpgradeSettingsConfiguration()
        {
            return false;
        }
    }
}
