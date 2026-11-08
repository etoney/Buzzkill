===============================================================================
  INSTALLING AN UNSIGNED VST3 / AU PLUGIN BY HAND
  (example plugin: BuzzKill by Castle Danger)
===============================================================================

This guide covers installing plugin files by hand (no installer), including
plugins that are not signed or notarized by Apple. Examples use BuzzKill, but the
steps work for any .vst3 or .component file.

For what every knob and button does, see controls.txt (included in this
download).

  - VST3 plugin file:  BuzzKill.vst3       (macOS and Windows)
  - AU plugin file:    BuzzKill.component  (macOS only)

On macOS, BuzzKill.vst3 and BuzzKill.component look like single files, but they are
really folders ("bundles"). Always move the whole thing. Never open it and copy
what's inside.


-------------------------------------------------------------------------------
CONTENTS
-------------------------------------------------------------------------------
  1. Before you start
  2. macOS: where plugins go
  3. macOS: if the VST3 or Components folder doesn't exist
  4. macOS: installing the VST3
  5. macOS: installing the AU (.component)
  6. macOS: unblocking an unsigned plugin (quarantine)
  7. Using subfolders to organize plugins
  8. Windows: installing the VST3
  9. Getting your DAW to see the plugin (Fender Studio Pro + others)
 10. Updating to a new version
 11. Uninstalling
 12. Troubleshooting


-------------------------------------------------------------------------------
1. BEFORE YOU START
-------------------------------------------------------------------------------
  a. Quit your DAW (Fender Studio Pro, Logic, Ableton, etc.) completely.
     Most DAWs only look for new plugins when they start up.

  b. Unzip the download. Double-click the .zip. You should end up with a
     folder holding something like:
         VST3/BuzzKill.vst3
         AU/BuzzKill.component        (macOS build only)
         Standalone/BuzzKill.app      (macOS) or BuzzKill.exe (Windows)

     If you've downloaded more than once, your browser may have named the newer
     file "BuzzKill-macOS (1).zip". Make sure you unzip the NEWEST one.

  c. Don't install the plugin from a folder it can't run from, like a
     disk image or the inside of a zip. Copy it into the folders below.


-------------------------------------------------------------------------------
2. macOS: WHERE PLUGINS GO
-------------------------------------------------------------------------------
There are two places on a Mac. Use ONE of them (either works).

  For just your user account (recommended, no admin password needed):
      VST3:  ~/Library/Audio/Plug-Ins/VST3/
      AU:    ~/Library/Audio/Plug-Ins/Components/

  For every user on the Mac (needs an admin password):
      VST3:  /Library/Audio/Plug-Ins/VST3/
      AU:    /Library/Audio/Plug-Ins/Components/

  "~" means your home folder (e.g. /Users/yourname). The ~/Library folder
  is hidden in Finder by default, so use one of these to get there:

  Option A, Finder "Go to Folder":
      1. Click the desktop or open any Finder window.
      2. Press Cmd + Shift + G.
      3. Paste:  ~/Library/Audio/Plug-Ins
      4. Press Return.

  Option B, Finder Go menu:
      1. In Finder, click the "Go" menu at the top of the screen.
      2. Hold down the Option key. "Library" appears in the menu.
      3. Click Library, then open Audio > Plug-Ins.

  Option C, Terminal (opens the folder in Finder):
      open ~/Library/Audio/Plug-Ins

  Don't install the same plugin in both places. Your DAW may load the wrong
  copy.


-------------------------------------------------------------------------------
3. macOS: IF THE VST3 OR COMPONENTS FOLDER DOESN'T EXIST
-------------------------------------------------------------------------------
This is normal. The folders only exist once some plugin has created them.
Making them yourself is fine. The names must be exact: "VST3" (all caps)
and "Components" (capital C).

  Option A, Terminal (easiest; creates every missing folder at once):
      1. Open Terminal (Applications > Utilities > Terminal, or press
         Cmd + Space and type "Terminal").
      2. Paste this line and press Return:

         mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components

      3. To open the folder in Finder afterwards:

         open ~/Library/Audio/Plug-Ins

     If the folders already exist, "mkdir -p" just does nothing, so it's
     always safe to run.

  Option B, Finder:
      1. Go to ~/Library/Audio/ (see section 2).
      2. If there's no "Plug-Ins" folder, right-click > New Folder and name
         it exactly:  Plug-Ins
      3. Open Plug-Ins. If "VST3" is missing, right-click > New Folder
         and name it exactly:  VST3
      4. If "Components" is missing, right-click > New Folder and name it
         exactly:  Components

  When you're done, the layout should look like this:

      ~/Library/
        Audio/
          Plug-Ins/
            VST3/
            Components/


-------------------------------------------------------------------------------
4. macOS: INSTALLING THE VST3
-------------------------------------------------------------------------------
  Finder:
      1. Open ~/Library/Audio/Plug-Ins/VST3/ (create it first if needed,
         see section 3).
      2. Drag BuzzKill.vst3 from the unzipped download into the VST3 folder.
         If asked about replacing an existing item, choose Replace.

  Terminal (replace the path with wherever you unzipped the download):
      cp -R ~/Downloads/BuzzKill-macOS/VST3/BuzzKill.vst3 ~/Library/Audio/Plug-Ins/VST3/

  Then do section 6 (unblock).


-------------------------------------------------------------------------------
5. macOS: INSTALLING THE AU (.component)
-------------------------------------------------------------------------------
Logic Pro and GarageBand ONLY load AU plugins. Fender Studio Pro, Ableton
and others can use either the AU or the VST3.

  Finder:
      1. Open ~/Library/Audio/Plug-Ins/Components/ (create it first if
         needed, see section 3).
      2. Drag BuzzKill.component into the Components folder.

  Terminal:
      cp -R ~/Downloads/BuzzKill-macOS/AU/BuzzKill.component ~/Library/Audio/Plug-Ins/Components/

  IMPORTANT: .component files must sit DIRECTLY inside the Components folder.
  macOS does not look in subfolders for AU plugins (see section 7).

  Then do section 6 (unblock).


-------------------------------------------------------------------------------
6. macOS: UNBLOCKING AN UNSIGNED PLUGIN (QUARANTINE)
-------------------------------------------------------------------------------
macOS marks every downloaded file as "quarantined". If a plugin isn't signed
and notarized by Apple, macOS may silently stop your DAW from loading it, or
show a "cannot be opened" / "is damaged" / "developer cannot be verified"
message. The fix is to remove the quarantine flag.

  1. Open Terminal.
  2. Paste this and press Return (it's one line):

     xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/BuzzKill.vst3 ~/Library/Audio/Plug-Ins/Components/BuzzKill.component

     - No output means it worked.
     - If you only installed one format, leave the other path out.
     - If you used a subfolder (section 7), use the full path to the
       plugin's actual location.
     - Easy path trick: type  xattr -dr com.apple.quarantine  followed
       by a space, then drag the plugin from Finder into the Terminal
       window to paste its exact path.

  3. To check it worked:
         xattr ~/Library/Audio/Plug-Ins/VST3/BuzzKill.vst3
     If "com.apple.quarantine" isn't listed, the plugin is unblocked.

  If macOS still blocks it:
     - Open System Settings > Privacy & Security, scroll down, and if a
       message about BuzzKill appears, click "Open Anyway" / "Allow Anyway".
     - On Apple Silicon Macs, a plugin with no signature at all can be
       refused. You can apply a free "ad-hoc" signature yourself:

       codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/BuzzKill.vst3
       codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/BuzzKill.component

  Repeat this section EVERY time you install a newly downloaded version.


-------------------------------------------------------------------------------
7. USING SUBFOLDERS TO ORGANIZE PLUGINS
-------------------------------------------------------------------------------
You can put VST3 plugins in subfolders, for example by company:

      ~/Library/Audio/Plug-Ins/VST3/Castle Danger/BuzzKill.vst3      (macOS)
      C:\Program Files\Common Files\VST3\Castle Danger\BuzzKill.vst3  (Windows)

  - VST3: subfolders are fine. DAWs search the VST3 folder AND every
    folder inside it.
  - AU (.component): subfolders DON'T work. macOS only loads AUs placed
    directly in the Components folder:
        CORRECT:  ~/Library/Audio/Plug-Ins/Components/BuzzKill.component
        WRONG:    ~/Library/Audio/Plug-Ins/Components/Castle Danger/BuzzKill.component

  To create a subfolder and install into it (macOS Terminal):

      mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3/Castle Danger"
      cp -R ~/Downloads/BuzzKill-macOS/VST3/BuzzKill.vst3 "$HOME/Library/Audio/Plug-Ins/VST3/Castle Danger/"
      xattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Castle Danger/BuzzKill.vst3"

  Keep the quotes when a folder name has spaces in it.

  In Finder: open the VST3 folder, right-click > New Folder, name it (e.g.
  "Castle Danger"), and drag BuzzKill.vst3 into it.

  Don't put a plugin inside ANOTHER plugin's bundle, and don't nest the
  plugin inside a folder that also ends in ".vst3".

  Custom folders outside the standard locations (e.g. an external drive) only
  work if your DAW lets you add that folder to its plugin search list
  (Studio Pro does, see section 9). The standard folders above are more
  reliable.


-------------------------------------------------------------------------------
8. WINDOWS: INSTALLING THE VST3
-------------------------------------------------------------------------------
Windows has no AU format. Use BuzzKill.vst3.

  Standard location:
      C:\Program Files\Common Files\VST3\

  If the VST3 folder doesn't exist:
      1. Open File Explorer and go to:  C:\Program Files\Common Files\
      2. Right-click > New > Folder and name it exactly:  VST3
      3. Click "Continue" when Windows asks for administrator permission.

      Or open Command Prompt as administrator (Start menu, type "cmd",
      right-click > Run as administrator) and run:
         mkdir "C:\Program Files\Common Files\VST3"

  Install:
      1. Copy BuzzKill.vst3 from the unzipped download into
         C:\Program Files\Common Files\VST3\
         (or into a subfolder such as ...\VST3\Castle Danger\)
      2. Approve the administrator prompt.

  Note: BuzzKill.vst3 may be a single file or a folder on Windows. Either way,
  copy it as-is.

  Unsigned plugins on Windows:
      - If the download was blocked: right-click the .zip BEFORE unzipping,
        choose Properties, tick "Unblock" at the bottom, click OK, then unzip.
      - If SmartScreen warns when opening the Standalone .exe, click
        "More info" > "Run anyway".


-------------------------------------------------------------------------------
9. GETTING YOUR DAW TO SEE THE PLUGIN
-------------------------------------------------------------------------------
FENDER STUDIO PRO
  1. Start Studio Pro after installing. It scans for new plugins on startup.
  2. Open the Browse panel > Effects. Look under "Castle Danger" or
     search for "BuzzKill".
  3. Drag BuzzKill onto your guitar track (put it FIRST in the chain, before
     any reverb or delay, so it hears the amp noise on its own).

  If it doesn't appear:
     a. Open the Locations settings:
          macOS:   Studio Pro menu > Preferences > Locations > VST Plug-ins
          Windows: Studio Pro menu > Options > Locations > VST Plug-ins
     b. Make sure your VST3 folder is listed. If not, click Add, choose
        the folder (or drag it in from Finder/Explorer), and click Apply.
     c. If BuzzKill ever crashed or failed to load, Studio Pro may have put it
        on its blocklist. Click "Reset Blocklist" on the same page.
     d. Restart Studio Pro.

LOGIC PRO / GARAGEBAND (AU only)
  - Logic Pro > Settings > Plug-in Manager. Find BuzzKill, select it,
    click "Reset & Rescan Selection".

ABLETON LIVE
  - Settings > Plug-ins. Turn on "Use VST3 Plug-In System Folders" (and
    "Use Audio Units" on Mac). Hold Option/Alt and click Rescan.

OTHER DAWs
  - Look for "Plug-in Manager", "Rescan", or "Plug-in Locations" in the
    settings, and make sure the standard VST3 folder is included.


-------------------------------------------------------------------------------
10. UPDATING TO A NEW VERSION
-------------------------------------------------------------------------------
Plugins and DAWs cache plugin info, so a new copy can look like the old one
unless you clear things out:

  1. Quit your DAW.
  2. Delete the OLD plugin first (drag it to the Trash, or see section 11).
  3. macOS only: clear the Audio Unit cache in Terminal:
        rm -rf ~/Library/Caches/AudioUnitCache
        killall -9 AudioComponentRegistrar 2>/dev/null
  4. Install the new files (sections 4 and 5, or 8).
  5. macOS: unblock again (section 6).
  6. Check which version is installed (macOS):
        defaults read ~/Library/Audio/Plug-Ins/VST3/BuzzKill.vst3/Contents/Info.plist CFBundleShortVersionString
  7. Start your DAW and load the plugin on a NEW track. Existing songs may
     have saved the old window size.
     BuzzKill shows its version (e.g. "V1.0.0") under its name.


-------------------------------------------------------------------------------
11. UNINSTALLING
-------------------------------------------------------------------------------
  macOS (Terminal):
      rm -rf ~/Library/Audio/Plug-Ins/VST3/BuzzKill.vst3
      rm -rf ~/Library/Audio/Plug-Ins/Components/BuzzKill.component
      (If you used a subfolder, remove it from there instead.)

  Windows:
      Delete C:\Program Files\Common Files\VST3\BuzzKill.vst3
      (approve the administrator prompt).


-------------------------------------------------------------------------------
12. TROUBLESHOOTING
-------------------------------------------------------------------------------
  Plugin doesn't show up at all
    - Check the folder name: exactly "VST3" or "Components".
    - Check the file is BuzzKill.vst3 itself, not a folder CONTAINING it
      (e.g. not VST3/VST3/BuzzKill.vst3 left over from the zip). A subfolder
      is fine for VST3 (section 7), but the .vst3 must sit inside it
      unchanged.
    - AU in a subfolder of Components? Move it directly into Components.
    - macOS: run the quarantine command (section 6).
    - Restart the DAW, or rescan/reset the blocklist (section 9).

  "BuzzKill is damaged and can't be opened" / "developer cannot be verified"
    - This is Gatekeeper blocking an unsigned plugin. Run the commands in
      section 6 (xattr, then codesign if needed).

  Old version still loads after updating
    - Follow section 10 exactly, especially deleting the old copy first and
      clearing the AU cache. Also check you haven't got a second copy in
      /Library/Audio/Plug-Ins/ (the all-users location).

  "Permission denied" in Terminal
    - Use the ~/Library (your user) folders, which need no password, or
      put "sudo " in front of the command for /Library and enter your
      Mac password (nothing shows while you type it; that's normal).

  Windows: "You need permission to perform this action"
    - Click Continue/Yes on the administrator prompt, or copy using an
      administrator account.

===============================================================================
