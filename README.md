# ClassLimit

ClassLimit helps you plan and track attendance across your subjects.

## Features

- **Subject Management**: Add subjects with their weekly hours and track them individually
- **Skip Counter**: Track how many classes you've skipped per subject with visual feedback (green/yellow/red)
- **Session-based Calculation**: Configure hours per session to calculate skips in sessions instead of individual hours
- **Smart Calculations**: Calculate how many classes/sessions you can skip per subject without dropping below required attendance
- **Persistent Storage**: Your subjects and settings are automatically saved using GSettings
- **Import/Export**: Export your subject list to JSON and import it later or share with others
- **GNOME HIG Compliance**: Modern Adwaita interface following GNOME Human Interface Guidelines

## How It Works

- Enter each subject and its hours per week
- Set required attendance percentage and total weeks in the semester
- Optionally configure hours per session (e.g., 2 hours = 1 session)
- Calculate to see how many classes/sessions you can skip per subject
- Track your actual skips with the +/- buttons
- Visual indicators show remaining skips (green = safe, yellow = low, red = over limit)

## Build and Run

Requirements: GTK 4, libadwaita 1.4+, and json-glib-1.0

```sh
meson setup builddir
ninja -C builddir
./builddir/src/classlimit
```

## Notes

- A "class" here equals one scheduled hour. E.g., a subject with 3 h/week over 15 weeks has 45 classes (hours).
- When session hours > 1, calculations are done per session instead of per hour
- Allowed skips are integers (floored)
- All data is automatically saved and persists between sessions
