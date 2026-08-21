// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Text.Json.Serialization;

using Settings.UI.Library.Attributes;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class RightClickLockProperties
    {
        [CmdConfigureIgnore]
        public HotkeySettings DefaultActivationShortcut => new HotkeySettings(true, false, true, false, 0x52); // Win + Alt + R

        [CmdConfigureIgnore]
        public HotkeySettings DefaultPanicShortcut => new HotkeySettings(false, true, true, false, 0x52); // Ctrl + Alt + R

        [JsonPropertyName("activation_shortcut")]
        public HotkeySettings ActivationShortcut { get; set; }

        [JsonPropertyName("panic_shortcut")]
        public HotkeySettings PanicShortcut { get; set; }

        [JsonPropertyName("auto_activate_in_game_mode")]
        public BoolProperty AutoActivateInGameMode { get; set; }

        [JsonPropertyName("hold_delay_ms")]
        public IntProperty HoldDelayMs { get; set; }

        [JsonPropertyName("move_cancel_pixels")]
        public IntProperty MoveCancelPixels { get; set; }

        public RightClickLockProperties()
        {
            ActivationShortcut = DefaultActivationShortcut;
            PanicShortcut = DefaultPanicShortcut;
            AutoActivateInGameMode = new BoolProperty(false);
            HoldDelayMs = new IntProperty(300);
            MoveCancelPixels = new IntProperty(10);
        }
    }
}
