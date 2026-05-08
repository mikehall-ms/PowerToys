// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

namespace Microsoft.PowerToys.Settings.UI.ViewModels
{
    public class MonitorDisplayInfo
    {
        public string DeviceName { get; set; }

        public string DisplayName { get; set; }

        public override string ToString() => DisplayName;
    }
}
