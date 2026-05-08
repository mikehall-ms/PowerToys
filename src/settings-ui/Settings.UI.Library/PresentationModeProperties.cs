// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Text.Json.Serialization;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class PresentationModeProperties
    {
        [JsonPropertyName("presentation_monitor")]
        public StringProperty PresentationMonitor { get; set; }

        // 0 = Move new apps AWAY from presentation monitor, 1 = Move new apps TO presentation monitor
        [JsonPropertyName("routing_mode")]
        public IntProperty RoutingMode { get; set; }

        [JsonPropertyName("excluded_apps")]
        public StringProperty ExcludedApps { get; set; }

        [JsonPropertyName("activation_shortcut")]
        public KeyboardKeysProperty ActivationShortcut { get; set; }

        public PresentationModeProperties()
        {
            PresentationMonitor = new StringProperty(string.Empty);
            RoutingMode = new IntProperty(0);
            ExcludedApps = new StringProperty(string.Empty);

            // Default activation shortcut: Win + Shift + P (VK 0x50)
            ActivationShortcut = new KeyboardKeysProperty(new HotkeySettings(true, false, false, true, 0x50));
        }
    }
}
