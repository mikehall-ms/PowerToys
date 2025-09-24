// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Runtime.CompilerServices;
using global::PowerToys.GPOWrapper;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.Library.Helpers;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.ViewModels
{
    /// <summary>
    /// Partial class implementation for Magnifier module functionality in MouseUtilsViewModel.
    /// This separates Magnifier-specific logic to allow for better module organization.
    /// </summary>
    public partial class MouseUtilsViewModel : PageViewModelBase
    {
        // Private fields for Magnifier
        private GpoRuleConfigured _magnifierEnabledGpoRuleConfiguration;
        private bool _magnifierEnabledStateIsGPOConfigured;
        private bool _isMagnifierEnabled;
        private double _magnifierMagnificationLevel;

        // Internal property for Magnifier settings configuration
        internal MagnifierSettings MagnifierSettingsConfig { get; set; }

        /// <summary>
        /// Initializes the Magnifier settings from the repository.
        /// </summary>
        /// <param name="magnifierSettingsRepository">The Magnifier settings repository</param>
        internal void InitializeMagnifierSettings(ISettingsRepository<MagnifierSettings> magnifierSettingsRepository)
        {
            ArgumentNullException.ThrowIfNull(magnifierSettingsRepository);

            try
            {
                this.MagnifierSettingsConfig = magnifierSettingsRepository.SettingsConfig;

                if (this.MagnifierSettingsConfig == null)
                {
                    return;
                }

                if (this.MagnifierSettingsConfig.Properties == null)
                {
                    return;
                }

                if (this.MagnifierSettingsConfig.Properties.MagnificationLevel == null)
                {
                    return;
                }

                // Initialize magnification level from settings
                _magnifierMagnificationLevel = this.MagnifierSettingsConfig.Properties.MagnificationLevel.Value;
            }
            catch (Exception)
            {
                throw;
            }
        }

        /// <summary>
        /// Initializes the Magnifier enabled values considering GPO configuration.
        /// </summary>
        internal void InitializeMagnifierEnabledValues()
        {
            try
            {
                _magnifierEnabledGpoRuleConfiguration = GPOWrapper.GetConfiguredMagnifierEnabledValue();

                if (_magnifierEnabledGpoRuleConfiguration == GpoRuleConfigured.Disabled || _magnifierEnabledGpoRuleConfiguration == GpoRuleConfigured.Enabled)
                {
                    // Get the enabled state from GPO.
                    _magnifierEnabledStateIsGPOConfigured = true;
                    _isMagnifierEnabled = _magnifierEnabledGpoRuleConfiguration == GpoRuleConfigured.Enabled;
                }
                else
                {
                    if (GeneralSettingsConfig?.Enabled == null)
                    {
                        _isMagnifierEnabled = false;
                    }
                    else
                    {
                        _isMagnifierEnabled = GeneralSettingsConfig.Enabled.Magnifier;
                    }
                }
            }
            catch (Exception)
            {
                // Set safe defaults if initialization fails
                _magnifierEnabledStateIsGPOConfigured = false;
                _isMagnifierEnabled = false;
            }
        }

        /// <summary>
        /// Gets or sets a value indicating whether the Magnifier module is enabled.
        /// </summary>
        public bool IsMagnifierEnabled
        {
            get => _isMagnifierEnabled;
            set
            {
                if (_magnifierEnabledStateIsGPOConfigured)
                {
                    // If it's GPO configured, shouldn't be able to change this state.
                    return;
                }

                if (_isMagnifierEnabled != value)
                {
                    _isMagnifierEnabled = value;

                    GeneralSettingsConfig.Enabled.Magnifier = value;
                    OnPropertyChanged(nameof(IsMagnifierEnabled));

                    OutGoingGeneralSettings outgoing = new OutGoingGeneralSettings(GeneralSettingsConfig);
                    SendConfigMSG(outgoing.ToString());

                    NotifyMagnifierPropertyChanged();
                }
            }
        }

        /// <summary>
        /// Gets a value indicating whether the Magnifier enabled state is configured by GPO.
        /// </summary>
        public bool IsMagnifierEnabledGpoConfigured
        {
            get => _magnifierEnabledStateIsGPOConfigured;
        }

        /// <summary>
        /// Gets or sets the activation shortcut for the Magnifier module.
        /// </summary>
        public HotkeySettings MagnifierActivationShortcut
        {
            get
            {
                return MagnifierSettingsConfig.Properties.ActivationShortcut;
            }

            set
            {
                if (MagnifierSettingsConfig.Properties.ActivationShortcut != value)
                {
                    MagnifierSettingsConfig.Properties.ActivationShortcut = value ?? MagnifierSettingsConfig.Properties.DefaultActivationShortcut;
                    NotifyMagnifierPropertyChanged();
                }
            }
        }

        /// <summary>
        /// Gets or sets the magnification level for the Magnifier module.
        /// </summary>
        public double MagnifierMagnificationLevel
        {
            get
            {
                return _magnifierMagnificationLevel;
            }

            set
            {
                if (Math.Abs(value - _magnifierMagnificationLevel) > 0.01)
                {
                    _magnifierMagnificationLevel = value;
                    MagnifierSettingsConfig.Properties.MagnificationLevel.Value = value;
                    NotifyMagnifierPropertyChanged();
                }
            }
        }

        /// <summary>
        /// Notifies that a Magnifier property has changed and persists the settings.
        /// </summary>
        /// <param name="propertyName">The name of the property that changed</param>
        public void NotifyMagnifierPropertyChanged([CallerMemberName] string propertyName = null)
        {
            try
            {
                OnPropertyChanged(propertyName);

                if (MagnifierSettingsConfig == null)
                {
                    return;
                }

                SndMagnifierSettings outsettings = new SndMagnifierSettings(MagnifierSettingsConfig);
                SndModuleSettings<SndMagnifierSettings> ipcMessage = new SndModuleSettings<SndMagnifierSettings>(outsettings);
                SendConfigMSG(ipcMessage.ToJsonString());
                SettingsUtils.SaveSettings(MagnifierSettingsConfig.ToJsonString(), MagnifierSettings.ModuleName);
            }
            catch (Exception)
            {
                throw;
            }
        }
    }
}
