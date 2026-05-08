// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.ObjectModel;
using global::PowerToys.GPOWrapper;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Helpers;
using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.Library.Helpers;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.ViewModels
{
    public partial class PresentationModeViewModel : PageViewModelBase
    {
        protected override string ModuleName => PresentationModeSettings.ModuleName;

        private GeneralSettings GeneralSettingsConfig { get; set; }

        private Func<string, int> SendConfigMSG { get; }

        private GpoRuleConfigured _enabledGpoRuleConfiguration;
        private bool _enabledStateIsGPOConfigured;
        private bool _isEnabled;

        private PresentationModeSettings _moduleSettings;

        private ObservableCollection<MonitorDisplayInfo> _monitors = new();
        private MonitorDisplayInfo _selectedMonitor;

        public PresentationModeSettings ModuleSettings => _moduleSettings;

        public ObservableCollection<MonitorDisplayInfo> Monitors => _monitors;

        public MonitorDisplayInfo SelectedMonitor
        {
            get => _selectedMonitor;
            set
            {
                if (_selectedMonitor != value)
                {
                    _selectedMonitor = value;
                    _moduleSettings.Properties.PresentationMonitor.Value = value?.DeviceName ?? string.Empty;
                    NotifySettingsChanged();
                    OnPropertyChanged(nameof(SelectedMonitor));
                }
            }
        }

        public PresentationModeViewModel(ISettingsRepository<GeneralSettings> settingsRepository, PresentationModeSettings moduleSettings, Func<string, int> ipcMSGCallBackFunc)
        {
            ArgumentNullException.ThrowIfNull(settingsRepository);

            GeneralSettingsConfig = settingsRepository.SettingsConfig;
            _moduleSettings = moduleSettings ?? new PresentationModeSettings();

            InitializeEnabledValue();

            SendConfigMSG = ipcMSGCallBackFunc ?? (_ => 0);

            RefreshMonitorList();
            Microsoft.Win32.SystemEvents.DisplaySettingsChanged += OnDisplaySettingsChanged;
        }

        private void InitializeEnabledValue()
        {
            try
            {
                _enabledGpoRuleConfiguration = GPOWrapper.GetConfiguredPresentationModeEnabledValue();
            }
            catch
            {
                _enabledGpoRuleConfiguration = GpoRuleConfigured.Unavailable;
            }

            if (_enabledGpoRuleConfiguration == GpoRuleConfigured.Disabled || _enabledGpoRuleConfiguration == GpoRuleConfigured.Enabled)
            {
                _enabledStateIsGPOConfigured = true;
                _isEnabled = _enabledGpoRuleConfiguration == GpoRuleConfigured.Enabled;
            }
            else
            {
                _isEnabled = GeneralSettingsConfig.Enabled.PresentationMode;
            }
        }

        public bool IsEnabled
        {
            get => _isEnabled;

            set
            {
                if (_enabledStateIsGPOConfigured)
                {
                    return;
                }

                if (value != _isEnabled)
                {
                    _isEnabled = value;
                    GeneralSettingsConfig.Enabled.PresentationMode = value;
                    OutGoingGeneralSettings snd = new OutGoingGeneralSettings(GeneralSettingsConfig);
                    SendConfigMSG(snd.ToString());
                    OnPropertyChanged(nameof(IsEnabled));
                }
            }
        }

        public bool IsEnabledGpoConfigured
        {
            get => _enabledStateIsGPOConfigured;
        }

        public int RoutingMode
        {
            get => _moduleSettings.Properties.RoutingMode.Value;
            set
            {
                if (_moduleSettings.Properties.RoutingMode.Value != value)
                {
                    _moduleSettings.Properties.RoutingMode.Value = value;
                    NotifySettingsChanged();
                    OnPropertyChanged(nameof(RoutingMode));
                }
            }
        }

        public string ExcludedApps
        {
            get => _moduleSettings.Properties.ExcludedApps.Value;
            set
            {
                if (_moduleSettings.Properties.ExcludedApps.Value != value)
                {
                    _moduleSettings.Properties.ExcludedApps.Value = value;
                    NotifySettingsChanged();
                    OnPropertyChanged(nameof(ExcludedApps));
                }
            }
        }

        public HotkeySettings ActivationShortcut
        {
            get => _moduleSettings.Properties.ActivationShortcut.Value;
            set
            {
                if (value != null && !value.Equals(_moduleSettings.Properties.ActivationShortcut.Value))
                {
                    _moduleSettings.Properties.ActivationShortcut.Value = value;
                    NotifySettingsChanged();
                    OnPropertyChanged(nameof(ActivationShortcut));
                }
            }
        }

        private void NotifySettingsChanged()
        {
            SendConfigMSG(string.Format(
                System.Globalization.CultureInfo.InvariantCulture,
                "{{ \"powertoys\": {{ \"{0}\": {1} }} }}",
                PresentationModeSettings.ModuleName,
                _moduleSettings.ToJsonString()));
        }

        public void ToggleRoutingMode()
        {
            // Flip the persisted routing mode and notify the runtime via custom action so
            // the engine picks up the change immediately.
            RoutingMode = RoutingMode == 0 ? 1 : 0;

            SendConfigMSG(string.Format(
                System.Globalization.CultureInfo.InvariantCulture,
                "{{ \"action\": {{ \"{0}\": {{ \"action_name\": \"Toggle\", \"value\": \"\" }} }} }}",
                PresentationModeSettings.ModuleName));
        }

        private void OnDisplaySettingsChanged(object sender, EventArgs e)
        {
            try
            {
                var settingsWindow = App.GetSettingsWindow();
                settingsWindow?.DispatcherQueue?.TryEnqueue(
                    Microsoft.UI.Dispatching.DispatcherQueuePriority.Normal,
                    RefreshMonitorList);
            }
            catch
            {
                // Settings window may not be available
            }
        }

        private void RefreshMonitorList()
        {
            var currentDeviceName = _moduleSettings.Properties.PresentationMonitor.Value;
            var newMonitors = EnumerateMonitors();

            _monitors.Clear();
            MonitorDisplayInfo toSelect = null;

            foreach (var monitor in newMonitors)
            {
                _monitors.Add(monitor);
                if (string.Equals(monitor.DeviceName, currentDeviceName, StringComparison.OrdinalIgnoreCase))
                {
                    toSelect = monitor;
                }
            }

            // Set selected without triggering settings save
            _selectedMonitor = toSelect;
            OnPropertyChanged(nameof(SelectedMonitor));
        }

        private static System.Collections.Generic.List<MonitorDisplayInfo> EnumerateMonitors()
        {
            var results = new System.Collections.Generic.List<MonitorDisplayInfo>();
            int monitorNumber = 0;

            for (uint deviceIndex = 0; ; deviceIndex++)
            {
                #pragma warning disable SA1129
                MonitorNativeMethods.DISPLAY_DEVICE device = default;
#pragma warning restore SA1129
                device.cb = (uint)System.Runtime.InteropServices.Marshal.SizeOf<MonitorNativeMethods.DISPLAY_DEVICE>();

                if (!MonitorNativeMethods.EnumDisplayDevices(null, deviceIndex, ref device, 0))
                {
                    break;
                }

                // Skip non-active adapters
                if ((device.StateFlags & 0x00000001u /* DISPLAY_DEVICE_ATTACHED_TO_DESKTOP */) == 0)
                {
                    continue;
                }

                monitorNumber++;
                string deviceName = device.DeviceName;

                // Get current display settings for resolution
                #pragma warning disable SA1129
                MonitorNativeMethods.DEVMODE devMode = default;
#pragma warning restore SA1129
                devMode.dmSize = (ushort)System.Runtime.InteropServices.Marshal.SizeOf<MonitorNativeMethods.DEVMODE>();

                int width = 0;
                int height = 0;
                if (MonitorNativeMethods.EnumDisplaySettings(deviceName, -1 /* ENUM_CURRENT_SETTINGS */, ref devMode))
                {
                    width = devMode.dmPelsWidth;
                    height = devMode.dmPelsHeight;
                }

                string displayName = width > 0
                    ? $"Monitor {monitorNumber} ({width}\u00D7{height})"
                    : $"Monitor {monitorNumber}";

                results.Add(new MonitorDisplayInfo
                {
                    DeviceName = deviceName,
                    DisplayName = displayName,
                });
            }

            return results;
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA1816:Dispose methods should call SuppressFinalize", Justification = "Base class PageViewModelBase.Dispose() handles GC.SuppressFinalize")]
        public override void Dispose()
        {
            Microsoft.Win32.SystemEvents.DisplaySettingsChanged -= OnDisplaySettingsChanged;
            base.Dispose();
        }

        public void RefreshEnabledState()
        {
            InitializeEnabledValue();
            OnPropertyChanged(nameof(IsEnabled));
        }
    }
}
