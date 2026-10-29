"""ros2bag CLI extension for the ReductStore storage plugin."""


def get_preset_profiles():
    """Return the default profile; settings are loaded from the YAML config."""
    return [
        ("none", "Use settings from --storage-config-file."),
    ]
