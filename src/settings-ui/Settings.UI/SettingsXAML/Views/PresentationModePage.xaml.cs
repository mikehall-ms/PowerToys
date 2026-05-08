// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using Microsoft.PowerToys.Settings.UI.Helpers;
using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.ViewModels;
using Microsoft.UI.Xaml;

namespace Microsoft.PowerToys.Settings.UI.Views
{
    public sealed partial class PresentationModePage : NavigablePage, IRefreshablePage
    {
        private PresentationModeViewModel ViewModel { get; set; }

        public PresentationModePage()
        {
            var settingsRepository = SettingsRepository<GeneralSettings>.GetInstance(SettingsUtils.Default);
            var moduleSettingsRepository = SettingsRepository<PresentationModeSettings>.GetInstance(SettingsUtils.Default);
            ViewModel = new PresentationModeViewModel(
                settingsRepository,
                moduleSettingsRepository.SettingsConfig,
                ShellPage.SendDefaultIPCMessage);
            DataContext = ViewModel;
            InitializeComponent();

            Loaded += (s, e) => ViewModel.OnPageLoaded();
        }

        public void RefreshEnabledState()
        {
            ViewModel.RefreshEnabledState();
        }

        private void ToggleNowButton_Click(object sender, RoutedEventArgs e)
        {
            ViewModel.ToggleRoutingMode();
        }
    }
}
