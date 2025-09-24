// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Text.Json.Serialization;

using ManagedCommon;
using Settings.UI.Library.Attributes;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class MagnifierProperties
    {
        [CmdConfigureIgnore]
        public HotkeySettings DefaultActivationShortcut => new HotkeySettings(true, false, true, false, 0xBC); // Win + Alt + Comma

        [JsonPropertyName("activation_shortcut")]
        public HotkeySettings ActivationShortcut { get; set; }

        [JsonPropertyName("magnification_level")]
        public DoubleProperty MagnificationLevel { get; set; }

        public MagnifierProperties()
        {
            try
            {
                var defaultShortcut = DefaultActivationShortcut;
                ActivationShortcut = DefaultActivationShortcut;
                MagnificationLevel = new DoubleProperty(2.0);
            }
            catch (Exception)
            {
                throw;
            }
        }
    }
}
