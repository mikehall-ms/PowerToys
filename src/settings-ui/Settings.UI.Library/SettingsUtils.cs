// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.IO;
using System.IO.Abstractions;
using System.Text.Json;

using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class SettingsUtils : ISettingsUtils
    {
        public const string DefaultFileName = "settings.json";
        private const string DefaultModuleName = "";
        private readonly IFile _file;
        private readonly ISettingsPath _settingsPath;

        private static readonly JsonSerializerOptions _serializerOptions = new JsonSerializerOptions
        {
            MaxDepth = 0,
            IncludeFields = true,
        };

        public SettingsUtils()
            : this(new FileSystem())
        {
        }

        public SettingsUtils(IFileSystem fileSystem)
            : this(fileSystem?.File, new SettingPath(fileSystem?.Directory, fileSystem?.Path))
        {
        }

        public SettingsUtils(IFile file, ISettingsPath settingPath)
        {
            _file = file ?? throw new ArgumentNullException(nameof(file));
            _settingsPath = settingPath;
        }

        public bool SettingsExists(string powertoy = DefaultModuleName, string fileName = DefaultFileName)
        {
            var settingsPath = _settingsPath.GetSettingsPath(powertoy, fileName);
            return _file.Exists(settingsPath);
        }

        public void DeleteSettings(string powertoy = "")
        {
            _settingsPath.DeleteSettings(powertoy);
        }

        public T GetSettings<T>(string powertoy = DefaultModuleName, string fileName = DefaultFileName)
            where T : ISettingsConfig, new()
        {
            if (!SettingsExists(powertoy, fileName))
            {
                throw new FileNotFoundException();
            }

            // Given the file already exists, to deserialize the file and read its content.
            T deserializedSettings = GetFile<T>(powertoy, fileName);

            // If the file needs to be modified, to save the new configurations accordingly.
            if (deserializedSettings.UpgradeSettingsConfiguration())
            {
                var upgradedJson = deserializedSettings.ToJsonString();
                SaveSettings(upgradedJson, powertoy, fileName);
            }

            return deserializedSettings;
        }

        /// <summary>
        /// Get a Deserialized object of the json settings string.
        /// This function creates a file in the powertoy folder if it does not exist and returns an object with default properties.
        /// </summary>
        /// <returns>Deserialized json settings object.</returns>
        public T GetSettingsOrDefault<T>(string powertoy = DefaultModuleName, string fileName = DefaultFileName)
            where T : ISettingsConfig, new()
        {
            try
            {
                var result = GetSettings<T>(powertoy, fileName);
                return result;
            }

            // Catch json deserialization exceptions when the file is corrupt and has an invalid json.
            // If there are any deserialization issues like in https://github.com/microsoft/PowerToys/issues/7500, log the error and create a new settings.json file.
            // This is different from the case where we have trailing zeros following a valid json file, which we have handled by trimming the trailing zeros.
            catch (JsonException ex)
            {
                Logger.LogError($"Exception encountered while loading {powertoy} settings.", ex);
            }
            catch (FileNotFoundException)
            {
                Logger.LogInfo($"Settings file {fileName} for {powertoy} was not found.");
            }

            // If the settings file does not exist or if the file is corrupt, to create a new object with default parameters and save it to a newly created settings file.
            T newSettingsItem = new T();
            SaveSettings(newSettingsItem.ToJsonString(), powertoy, fileName);
            return newSettingsItem;
        }

        /// <summary>
        /// Get a Deserialized object of the json settings string.
        /// This function creates a file in the powertoy folder if it does not exist and returns an object with default properties.
        /// </summary>
        /// <returns>Deserialized json settings object.</returns>
        public T GetSettingsOrDefault<T, T2>(string powertoy = DefaultModuleName, string fileName = DefaultFileName, Func<object, object> settingsUpgrader = null)
            where T : ISettingsConfig, new()
            where T2 : ISettingsConfig, new()
        {
            try
            {
                return GetSettings<T>(powertoy, fileName);
            }

            // Catch json deserialization exceptions when the file is corrupt and has an invalid json.
            // If there are any deserialization issues like in https://github.com/microsoft/PowerToys/issues/7500, log the error and create a new settings.json file.
            // This is different from the case where we have trailing zeros following a valid json file, which we have handled by trimming the trailing zeros.
            catch (JsonException ex)
            {
                Logger.LogInfo($"Settings file {fileName} for {powertoy} was unrecognized. Possibly containing an older version. Trying to read again.");

                // try to deserialize to the old format, which is presented in T2
                try
                {
                    T2 oldSettings = GetSettings<T2>(powertoy, fileName);
                    T newSettings = (T)settingsUpgrader(oldSettings);
                    Logger.LogInfo($"Settings file {fileName} for {powertoy} was read successfully in the old format.");

                    // If the file needs to be modified, to save the new configurations accordingly.
                    if (newSettings.UpgradeSettingsConfiguration())
                    {
                        SaveSettings(newSettings.ToJsonString(), powertoy, fileName);
                    }

                    return newSettings;
                }
                catch (Exception)
                {
                    // do nothing, the problem wasn't that the settings was stored in the previous format, continue with the default settings
                    Logger.LogError($"{powertoy} settings are corrupt or the format is not supported any longer. Using default settings instead.", ex);
                }
            }
            catch (FileNotFoundException)
            {
                Logger.LogInfo($"Settings file {fileName} for {powertoy} was not found.");
            }

            // If the settings file does not exist or if the file is corrupt, to create a new object with default parameters and save it to a newly created settings file.
            T newSettingsItem = new T();
            SaveSettings(newSettingsItem.ToJsonString(), powertoy, fileName);
            return newSettingsItem;
        }

        // Given the powerToy folder name and filename to be accessed, this function deserializes and returns the file.
        private T GetFile<T>(string powertoyFolderName = DefaultModuleName, string fileName = DefaultFileName)
        {
            Logger.LogInfo($"[SettingsUtils] GetFile<{typeof(T).Name}> starting: powertoy='{powertoyFolderName}', fileName='{fileName}'");

            try
            {
                var fullPath = _settingsPath.GetSettingsPath(powertoyFolderName, fileName);
                Logger.LogInfo($"[SettingsUtils] GetFile: Full path resolved to '{fullPath}'");

                // Adding Trim('\0') to overcome possible NTFS file corruption.
                // Look at issue https://github.com/microsoft/PowerToys/issues/6413 you'll see the file has a large sum of \0 to fill up a 4096 byte buffer for writing to disk
                // This, while not totally ideal, does work around the problem by trimming the end.
                // The file itself did write the content correctly but something is off with the actual end of the file, hence the 0x00 bug
                Logger.LogInfo($"[SettingsUtils] GetFile: Reading file content from '{fullPath}'");
                var jsonSettingsString = _file.ReadAllText(fullPath).Trim('\0');
                Logger.LogInfo($"[SettingsUtils] GetFile: File content read, length={jsonSettingsString.Length} chars, first 100 chars: '{(jsonSettingsString.Length > 100 ? jsonSettingsString.Substring(0, 100) : jsonSettingsString)}'");

                try
                {
                    var options = _serializerOptions;
                    Logger.LogInfo($"[SettingsUtils] GetFile: Attempting JSON deserialization to type {typeof(T).Name}");
                    Logger.LogInfo($"[SettingsUtils] GetFile: SerializerOptions - MaxDepth={options.MaxDepth}, IncludeFields={options.IncludeFields}");

                    var result = JsonSerializer.Deserialize<T>(jsonSettingsString, options);
                    Logger.LogInfo($"[SettingsUtils] GetFile: JSON deserialization successful for type {typeof(T).Name}");
                    return result;
                }
                catch (JsonException jsonEx)
                {
                    Logger.LogError($"[SettingsUtils] GetFile: JSON deserialization failed for type {typeof(T).Name}: {jsonEx.Message}");
                    Logger.LogError($"[SettingsUtils] GetFile: JSON content causing error: '{jsonSettingsString}'");
                    throw;
                }
                catch (Exception innerEx)
                {
                    Logger.LogError($"[SettingsUtils] GetFile: Unexpected deserialization error for type {typeof(T).Name}: {innerEx.Message}");
                    Logger.LogError($"[SettingsUtils] GetFile: JSON content: '{jsonSettingsString}'");
                    throw;
                }
            }
            catch (Exception ex)
            {
                Logger.LogError($"[SettingsUtils] GetFile: Exception in GetFile<{typeof(T).Name}> for '{powertoyFolderName}'/'{fileName}': {ex.Message}");
                Logger.LogError($"[SettingsUtils] GetFile: Exception details: {ex}");
                throw;
            }
        }

        // Save settings to a json file.
        public void SaveSettings(string jsonSettings, string powertoy = DefaultModuleName, string fileName = DefaultFileName)
        {
            Logger.LogInfo($"[SettingsUtils] SaveSettings starting: powertoy='{powertoy}', fileName='{fileName}', jsonLength={jsonSettings?.Length ?? 0}");

            try
            {
                if (jsonSettings != null)
                {
                    Logger.LogInfo($"[SettingsUtils] SaveSettings: Checking if settings folder exists for '{powertoy}'");
                    if (!_settingsPath.SettingsFolderExists(powertoy))
                    {
                        Logger.LogInfo($"[SettingsUtils] SaveSettings: Settings folder does not exist, creating for '{powertoy}'");
                        _settingsPath.CreateSettingsFolder(powertoy);
                        Logger.LogInfo($"[SettingsUtils] SaveSettings: Successfully created settings folder for '{powertoy}'");
                    }
                    else
                    {
                        Logger.LogInfo($"[SettingsUtils] SaveSettings: Settings folder already exists for '{powertoy}'");
                    }

                    var targetPath = _settingsPath.GetSettingsPath(powertoy, fileName);
                    Logger.LogInfo($"[SettingsUtils] SaveSettings: Writing settings to file '{targetPath}'");
                    Logger.LogInfo($"[SettingsUtils] SaveSettings: JSON content preview (first 200 chars): '{(jsonSettings.Length > 200 ? jsonSettings.Substring(0, 200) : jsonSettings)}'");

                    _file.WriteAllText(targetPath, jsonSettings);
                    Logger.LogInfo($"[SettingsUtils] SaveSettings: Successfully wrote settings to '{targetPath}'");
                }
                else
                {
                    Logger.LogWarning($"[SettingsUtils] SaveSettings: jsonSettings is null, skipping save for {powertoy}/{fileName}");
                }
            }
            catch (Exception e)
            {
                Logger.LogError($"[SettingsUtils] SaveSettings: Exception encountered while saving {powertoy} settings to {fileName}: {e.Message}", e);
                Logger.LogError($"[SettingsUtils] SaveSettings: Full exception details: {e}");
#if DEBUG
                if (e is ArgumentException || e is ArgumentNullException || e is PathTooLongException)
                {
                    Logger.LogError($"[SettingsUtils] SaveSettings: Re-throwing critical exception in DEBUG mode");
                    throw;
                }
#endif
            }
        }

        // Returns the file path to the settings file, that is exposed from the local ISettingsPath instance.
        public string GetSettingsFilePath(string powertoy = "", string fileName = "settings.json")
        {
            return _settingsPath.GetSettingsPath(powertoy, fileName);
        }

        /// <summary>
        /// Method <c>BackupSettings</c> Mostly a wrapper for SettingsBackupAndRestoreUtils.BackupSettings
        /// </summary>
        public static (bool Success, string Message, string Severity, bool LastBackupExists, string OptionalMessage) BackupSettings()
        {
            var settingsBackupAndRestoreUtilsX = SettingsBackupAndRestoreUtils.Instance;
            var settingsUtils = new SettingsUtils();
            var appBasePath = Path.GetDirectoryName(settingsUtils._settingsPath.GetSettingsPath(string.Empty, string.Empty));
            string settingsBackupAndRestoreDir = settingsBackupAndRestoreUtilsX.GetSettingsBackupAndRestoreDir();

            return settingsBackupAndRestoreUtilsX.BackupSettings(appBasePath, settingsBackupAndRestoreDir, false);
        }

        /// <summary>
        /// Method <c>RestoreSettings</c> Mostly a wrapper for SettingsBackupAndRestoreUtils.RestoreSettings
        /// </summary>
        public static (bool Success, string Message, string Severity) RestoreSettings()
        {
            var settingsBackupAndRestoreUtilsX = SettingsBackupAndRestoreUtils.Instance;
            var settingsUtils = new SettingsUtils();
            var appBasePath = Path.GetDirectoryName(settingsUtils._settingsPath.GetSettingsPath(string.Empty, string.Empty));
            string settingsBackupAndRestoreDir = settingsBackupAndRestoreUtilsX.GetSettingsBackupAndRestoreDir();
            return settingsBackupAndRestoreUtilsX.RestoreSettings(appBasePath, settingsBackupAndRestoreDir);
        }
    }
}
