# CrossPoint User Guide

Welcome to the **CrossPoint** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [CrossPoint User Guide](#crosspoint-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
    - [Taking a Screenshot](#taking-a-screenshot)
    - [Frontlight (X4 Pro only)](#frontlight-x4-pro-only)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 File Transfer Screen](#35-file-transfer-screen)
    - [3.5.1 Calibre Wireless Transfers](#351-calibre-wireless-transfers)
      - [Installing the Plugin in Calibre](#installing-the-plugin-in-calibre)
      - [Configuring the CrossPoint Plugin in Calibre](#configuring-the-crosspoint-plugin-in-calibre)
      - [Uploading Books](#uploading-books)
      - [Removing a Book](#removing-a-book)
    - [3.6 Settings](#36-settings)
      - [3.6.1 Display](#361-display)
      - [3.6.2 Reader](#362-reader)
      - [3.6.3 Controls](#363-controls)
      - [3.6.4 System](#364-system)
      - [3.6.5 OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries)
      - [3.6.6 Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds)
      - [3.6.7 KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)
        - [Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)](#option-a-crosspoint-sync-server-synccrosspointreadercom-default)
        - [Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)](#option-b-legacy-public-koreader-server-synckoreaderrocks)
        - [Option C: Self-Hosted Server (Docker Compose)](#option-c-self-hosted-server-docker-compose)
        - [Syncing While Reading](#syncing-while-reading)
    - [3.7 Sleep Screen](#37-sleep-screen)
      - [Cover settings](#cover-settings)
      - [Custom images](#custom-images)
    - [3.8 Custom Fonts (SD Card)](#38-custom-fonts-sd-card)
    - [3.9 GitHub Library Sync](#39-github-library-sync)
      - [What You Need](#what-you-need)
      - [Creating the Access Token](#creating-the-access-token)
      - [Writing the Configuration File](#writing-the-configuration-file)
      - [Preparing the First Sync](#preparing-the-first-sync)
      - [Running an Update](#running-an-update)
      - [What Update Changes](#what-update-changes)
      - [What Update Never Touches](#what-update-never-touches)
      - [Limits and Skipped Files](#limits-and-skipped-files)
      - [Messages and What to Do](#messages-and-what-to-do)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [Auto Page Turn](#auto-page-turn)
    - [Tilt Page Turn (X3 only)](#tilt-page-turn-x3-only)
    - [Footnote Navigation](#footnote-navigation)
    - [Dictionary Lookup](#dictionary-lookup)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Reader Menu](#5-reader-menu)
    - [5.1 Chapter Selection](#51-chapter-selection)
    - [5.2 Bookmarks](#52-bookmarks)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)

## 1. Hardware Overview

The device utilises the standard buttons on the Xteink X4 (in the same layout as the manufacturer firmware, by default):

### Button Layout

| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Side Up**, **Side Down**, **Reset** |

Button layout can be customized in the **[Controls Settings](#363-controls)**.

### Taking a Screenshot

When the Power button and the lower side button (Side Down) are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

### Frontlight (X4 Pro only)

The X4 Pro has a built-in frontlight with adjustable brightness and warmth. It is controlled from a swipe panel rather than the Settings menu:

* **Open the frontlight panel:** Swipe down from the top edge of the screen, from almost any screen (Home, Browse Files, Reading Mode, etc.). Drag the brightness and warmth sliders to adjust the light live, or tap the sun icon to turn it on or off.
* **Quick toggle:** Double-click the **Power** button to turn the frontlight on or off instantly, without opening the panel.

> [!NOTE]
> Frontlight brightness and warmth are intentionally not listed in **[Display Settings](#361-display)** — the swipe panel is the only place to adjust them. The on/off state can also be toggled with the Power-button double-click above.

If the frontlight doesn't come back on after the device wakes from sleep, check **Restore Light on Wake** in **[Display Settings](#361-display)** (on by default). Turning it off is intentional if you'd rather have the light stay off on wake and switch it on yourself each time — but it's easy to forget you changed it.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#363-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. From here you can navigate to **[Reading Mode](#4-reading-mode)** with the most recently read book, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#35-file-transfer-screen)** screen, or **[Settings](#36-settings)**. When a GitHub sync configuration is set up, the Home screen also shows **Update Library** (see [GitHub Library Sync](#39-github-library-sync)).

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser. The full path to the current directory is shown at the top of the screen. File extensions are displayed alongside each filename, and directories are shown with brackets (e.g. `[folder-name]`). Hidden directories (those beginning with `.`) are also visible.

* **Navigate List:** Use **Left** (or **Side Up**), or **Right** (or **Side Down**) to move the selection cursor up and down through folders and books. You can also long-press these buttons to scroll a full page up or down.
* **Open Selection:** Press **Confirm** to open a folder or start reading a selected book. Selecting a `.bmp` file will open the image viewer.
* **Delete Files or Folders:** Hold and release **Confirm** to delete the selected file or folder. You will be given an option to either confirm or cancel. Multiple files can be selected for deletion in a single operation.
* **Rename or Move:** Files can be renamed or moved to a different folder from within the browse screen.

### 3.4 Recent Books Screen

The Recent Books screen lists the most recently opened books in a chronological view, displaying title and author.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload and manage files on the device. When you enter the screen, choose **Join a Network**, **Calibre Wireless**, or **Create Hotspot**. The reader then starts the web server for the selected mode.

See the [web server docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

The web interface also supports **WebDAV**, allowing you to mount the device as a network drive and manage files directly from your computer's file manager.

Download links for files already on the device are available in the web interface, so you can retrieve books or screenshots over Wi-Fi without connecting a cable.

A **Wi-Fi signal strength indicator** (dBm) is displayed on-screen during joined-network web server sessions.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [web server docs](./docs/webserver.md) for details.
> [!TIP]
> If your EPUBs have compatibility issues, you can run the built-in **EPUB Optimizer** directly from the device to clean up and reprocess books for better rendering.

### 3.5.1 Calibre Wireless Transfers

CrossPoint supports sending books from Calibre using the CrossPoint Reader device plugin.

#### Installing the Plugin in Calibre

If you don't already have the plugin installed:

1. Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
2. Download the zip file.
3. Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
4. Restart Calibre.

#### Configuring the CrossPoint Plugin in Calibre
1. In Calibre select Preferences.
2. In the Preferences dialog select Plugins.
3. In Plugins search for "crosspoint".
4. Click on "Customize plugin".
5. Update the value for "Host" to match the IP for your device.
6. Leave the other settings as they are.
7. [optional] Modify the "Upload path" to point to a subfolder other than the root "/" folder. Enter this as a path relative to the root folder. Example: `/mybooks`
8. Restart Calibre.

<img width="420" height="385" alt="Image" src="https://github.com/user-attachments/assets/01fc7e33-a9a7-48ba-9e26-2e68d1f9daec" />

#### Uploading Books

To upload a book using the CrossPoint plugin in Calibre:

1. On the device: File Transfer -> Calibre Wireless, then join a network.
2. Select one or more books.
3. Right-click on that selection.
4. Select "Send to Device" > "Send to main memory"

The CrossPoint plugin will connect to your device, create a folder for the book's author in the root folder (or the folder you configured for the plugin), then copy the book into that folder.

<img width="783" height="310" alt="Image" src="https://github.com/user-attachments/assets/741b0909-2e1d-4f16-8af0-2c43fbda5ce6" />

#### Removing a Book

Books cannot be removed from your device through Calibre. Use the web interface instead.

### 3.6 Settings

The Settings screen allows you to configure the device's behavior. There are a few settings you can adjust:

#### 3.6.1 Display

- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  
  - "Dark" (default) - The default dark Crosspoint logo sleep screen
  - "Light" - The same default sleep screen, on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#37-sleep-screen) below for more information
  - "Cover" - The book cover image (Note: this is experimental and may not work as expected)
  - "None" - A blank screen
  - "Cover + Custom" - The book cover image while actively reading, falls back to "Custom" behavior otherwise
  - "Quick resume" - The text of the last page read will be displayed on the sleep screen and a moon icon is shown on the edge of the screen. Waking up the device will return to the same page of the opened book. This is useful for quickly resuming reading without waiting for the device to fully wake up and load the book.
  - "Transparent" - A transparent overlay image drawn over the current screen; see [Sleep Screen](#37-sleep-screen) below for more information
- **Sleep Screen Cover Mode**: How to display the book cover when "Cover" sleep screen is selected:
  
  - "Fit" (default) - Scale the image down to fit centered on the screen, padding with white borders as necessary
  - "Crop" - Scale the image down and crop as necessary to try to fill the screen (Note: this is experimental and may not work as expected)

- **Sleep Screen Cover Filter**: What filter will be applied to the book cover when "Cover" sleep screen is selected:
  
  - "None" (default) - The cover image will be converted to a grayscale image and displayed as it is
  - "Contrast" - The image will be displayed as a black & white image without grayscale conversion
  - "Inverted" - The image will be inverted as in white & black and will be displayed without grayscale conversion

- **Quick Resume on Timeout**: Whether to enable the "Quick Resume" sleep screen when the device goes to sleep due to inactivity (System > Time to Sleep). This is useful for quickly resuming reading without waiting for the device to fully wake up and load the book. This overwrites the Sleep Screen Cover Mode when enabled.

- **Status Bar**: Configure the status bar displayed while reading:
  
  - "None" - No status bar
  - "No Progress" - Show status bar without reading progress
  - "Full w/ Percentage" - Show status bar with book progress (as percentage)
  - "Full w/ Book Bar" - Show status bar with book progress (as bar)
  - "Book Bar Only" - Show book progress (as bar)
  - "Full w/ Chapter Bar" - Show status bar with chapter progress (as bar)

- **Hide Battery %**: Configure where to suppress the battery percentage display in the status bar; the battery icon will still be shown:
  
  - "Never" (default) - Always show battery percentage
  - "In Reader" - Show battery percentage everywhere except in reading mode
  - "Always" - Always hide battery percentage

- **Refresh Frequency**: Set how often the screen does a full refresh while reading to reduce ghosting; options are every 1, 5, 10, 15, or 30 pages.

- **UI Theme**: Set which UI theme to use:
  
  - "Classic" - The original Crosspoint theme
  - "Lyra" - The new theme for Crosspoint featuring rounded elements and menu icons
  - "Lyra Extended" - Lyra, but displays 3 books instead of 1 on the **[Home Screen](#31-home-screen)**
  - "RoundedRaff" - A rounded theme with additional visual styling

- **Sunlight Fading Fix**: Configure whether to enable a software-fix for the issue where white X4 models may fade when used in direct sunlight:
  
  - "OFF" (default) - Disable the fix
  - "ON" - Enable the fix

> [!NOTE]
> A battery charging indicator is shown on the battery icon whenever the device is actively charging.

#### 3.6.2 Reader

- **Reader Font Family**: Choose the font used for reading:
  
  - "Noto Serif" (default) - Google's serif font
  - "Noto Sans" - Google's sans-serif font

- **Reader Font Size**: Adjust the text size for reading; options are "Small", "Medium" (default), "Large", or "X Large".

- **Reader Line Spacing**: Adjust the spacing between lines; options are "Tight", "Normal" (default), or "Wide".

- **Reader Screen Margin**: Controls the screen margins in Reading Mode between 5 and 40 pixels in 5-pixel increments.

- **Reader Paragraph Alignment**: Set the alignment of paragraphs; options are "Justified" (default), "Left", "Center", or "Right".

- **Embedded Style**: Whether to use the EPUB file's embedded HTML and CSS stylisation and formatting; options are "ON" or "OFF".

- **Hyphenation**: Whether to hyphenate text in Reading Mode; options are "ON" or "OFF".

- **Reading Orientation**: Set the screen orientation for reading EPUB files:
  
  - "Portrait" (default) - Standard portrait orientation
  - "Landscape CW" - Landscape, rotated clockwise
  - "Inverted" - Portrait, upside down
  - "Landscape CCW" - Landscape, rotated counter-clockwise

- **Extra Paragraph Spacing**: Set how to handle paragraph breaks:
  
  - "ON" - Vertical space will be added between paragraphs in Reading Mode
  - "OFF" - Paragraphs will not have vertical space added, but will have first-line indentation

- **Dictionary**: Select the StarDict dictionary used for word lookups while reading, or "None" to disable lookups. *(Only shown when at least one dictionary folder exists under `/dictionaries/` on the SD card — see [docs/dictionary.md](docs/dictionary.md) for setup and usage.)*

- **Text Anti-Aliasing**: Whether to show smooth grey edges (anti-aliasing) on text in reading mode. Note this slows down page turns slightly.

- **Images**: Whether to display embedded images (JPG/PNG) found in EPUB files; options are "ON" (default) or "OFF".

- **Focus Reading**: Bolds the first part of each word to create visual fixation points, similar to Bionic Reading. This can help improve reading speed and focus; options are "ON" or "OFF" (default).

#### 3.6.3 Controls

- **Remap Front Buttons**: A menu for customising the function of each bottom edge button.

- **Side Button Layout (reader)**: Swap the order of the up and down side buttons from "Prev/Next" (default) to "Next/Prev". You can also disable them entirely. This change is only in effect when reading.

- **Long-press Chapter Skip**: Set whether long-pressing page turn buttons skips to the next/previous chapter:
  
  - "Chapter Skip" (default) - Long-pressing skips to next/previous chapter
  - "Page Scroll" - Long-pressing scrolls a page up/down
- **Long-press Menu**: Selects the function bound to holding the menu button (Confirm) while reading an EPUB. **Cycles through the available functions** each time the setting is selected — additional functions may be added in future releases, so this is not a binary on/off toggle. A short press of Confirm always opens the reader menu as normal:
  - "Bookmark" (default) - Hold Confirm (~0.4 second) to drop a bookmark at the current page.
  - "KOSync" - Hold Confirm (~1 second) to launch KOReader sync directly.
  - "Dictionary" - Hold Confirm (~0.4 second) to start dictionary word selection on the current page (see [docs/dictionary.md](docs/dictionary.md)).
  - "Disabled" - Long-press is ignored; only short-press opens the reader menu.

- **Short Power Button Click**: Controls the effect of a short click of the power button:
  
  - "Ignore" (default) - Require a long press to turn off the device
  - "Sleep" - A short press puts the device into sleep mode
  - "Page Turn" - A short press in reading mode turns to the next page; a long press turns the device off
  - "Footnotes" - A short press in reading mode opens the footnotes submenu; if only one footnote is present on the page, the referenced page is opened directly. The short press on the power button can be used to select the footnote in the submenu, and to go back to the original page after finish reading the footnote (like the back button).
  - "Refresh" - A short press triggers a manual full-screen refresh, useful for clearing ghosting
- **Quick-return from footnotes**: Toggles on and off the quick return functionality from the footnotes. When the functionality it's active, a short press of the power button will act as the back button from the footnotes page.

#### 3.6.4 System

- **Time to Sleep**: Set the duration of inactivity before the device automatically goes to sleep; options are 1, 3, 5, 10 (default), 15 or 30 minutes.

- **Wi-Fi Networks**: Connect to Wi-Fi networks for file transfers and firmware updates.

- **KOReader Sync**: Options for setting up KOReader for syncing book progress. **Smart sync** is the default for new configurations and auto-resolves simple push/pull decisions. Existing credential files retain **Ask every time** when migrated; you can switch Sync Behavior at any time if you prefer manual confirmation.

- **OPDS Servers**: Manage one or more OPDS [(Open Publication Distribution System)](https://en.wikipedia.org/wiki/Open_Publication_Distribution_System) libraries for browsing and downloading books. See [OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries) below.

- **Clear Reading Cache**: Clear the internal SD card cache.

- **Check for updates**: Check for Crosspoint firmware updates over Wi-Fi. Firmware can also be updated without a USB connection by placing a `firmware.bin` file on the SD card.

- **Language**: Set the UI language. CrossPoint supports 24 languages: English, Spanish, French, German, Czech, Brazilian Portuguese, Russian, Swedish, Romanian, Catalan, Ukrainian, Belarusian, Italian, Polish, Finnish, Danish, Dutch, Turkish, Kazakh, Hungarian, Lithuanian, Slovenian, Valencian, and Hebrew.

- **Manage Fonts**: Browse, download, and manage custom font families installed from the SD card. See [Custom Fonts (SD Card)](#38-custom-fonts-sd-card) for more information.

#### 3.6.5 OPDS Servers (Multiple Libraries)

CrossPoint supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings -> System -> OPDS Servers**.

2. Select **Add Server** to create a new entry, or select an existing server to edit it.

3. Configure these fields:
   
   - **Server Name**: Optional display name (for example, "Home Calibre" or "Public Catalog").
   
   - **OPDS Server URL**: Full catalog root URL (for Calibre Content Server, usually ends with `/opds`).
   
   - **Username / Password**: Optional credentials for authenticated servers.

4. Use **Delete Server** inside a server entry to remove it.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS authentication supports HTTP Basic auth. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.

For web-based Wi-Fi network management, see [Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds).

#### 3.6.6 Web Settings (Wi-Fi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **Wi-Fi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect through **Join a Network** or **Create Hotspot**.
2. In a browser, open `http://<device-ip>/settings` or `http://crosspoint.local`.
3. In **Wi-Fi Networks**, add, edit, or delete saved network entries (SSID + optional password).
4. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs, but connecting to hidden networks still depends on the device-side Wi-Fi connection flow.

#### 3.6.7 KOReader Sync Quick Setup

CrossPoint can sync reading progress with KOReader-compatible sync servers.
It also interoperates with KOReader apps/devices when they use the same server and credentials.

##### Option A: CrossPoint Sync Server (`sync.crosspointreader.com`, default)

When **Sync Server URL** is left empty, CrossPoint uses the free CrossPoint sync server at `https://sync.crosspointreader.com`. It speaks the standard KOReader sync protocol (so KOReader apps can use it too). CrossPoint records page starts as chapter-content offsets and sends the corresponding standard KOReader XPath, so devices with different fonts or layouts can return to the same text.

1. On each CrossPoint device:

   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).

   - Leave **Sync Server URL** empty (or set it to `https://sync.crosspointreader.com`).

   - On the first device, run **Sign Up** once to create the account directly from the device. On every other device, just run **Authenticate**.

Accounts are per server. Existing `sync.koreader.rocks` credentials do not exist on the CrossPoint server; either sign up again with the same username/password or use Option B to keep using the legacy server.

##### Option B: Legacy Public KOReader Server (`sync.koreader.rocks`)

Use this if you already sync KOReader devices against the official public server.

1. On each CrossPoint device:

   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Sync Server URL** to `https://sync.koreader.rocks` (required; an empty URL now points at the CrossPoint server instead).

   - Set **Username** and **Password** to your existing KOReader Sync credentials.

   - Run **Authenticate**.

2. If you do not have an account yet, run **Sign Up** on the device, or register once with curl:

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "https://sync.koreader.rocks/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

When this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, pick a different username or use that existing account.

##### Option C: Self-Hosted Server (Docker Compose)

1. Start a sync server:

```bash
mkdir -p kosync-quickstart
cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

# Docker
docker compose up -d

# Podman (alternative)
podman compose up -d
```

> [!NOTE]
> `ENABLE_USER_REGISTRATION=true` is convenient for first setup. After creating your users, set it to `false` (or remove it) to avoid unexpected registrations.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once.
   CrossPoint authenticates against KOReader Sync (`koreader/kosync`) using an MD5 key, so register using the MD5 of your password:

> [!WARNING]
> Sending a reusable MD5-derived password over plain HTTP is insecure.
> Create unique sync-only credentials and do not reuse main account passwords.
> Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN or when using untrusted networks.
> Use `curl -k` only for self-signed certificate testing.

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "http://<server-ip>:17200/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

If this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, the account already exists.

4. On each CrossPoint device:
   
   - Go to **Settings -> System -> KOReader Sync**.
   
   - Set **Username** and **Password** (enter the plain password; CrossPoint computes MD5 internally, and use the same values on all devices).
   
   - Set **Sync Server URL** to `http://<server-ip>:17200`.
   
   - Run **Authenticate**.

If you use the HTTPS listener, use `https://<server-ip>:7200` (`curl -k` only for self-signed certificate testing).

##### Syncing While Reading

Once any of the options above is set up, press **Confirm** while reading to open the reader menu, then select **Sync Progress**. Alternatively, set **Settings -> Controls -> Long-press Menu** to **KOSync** and hold Confirm to launch sync directly.

- With **Sync Behavior** set to **Ask every time**, choose **Apply Remote** to jump to remote progress or **Upload Local** to push current progress.
- With **Sync Behavior** set to **Smart sync**, CrossPoint auto-resolves simple cases: upload when no remote progress exists, confirm and leave both unchanged when local and remote progress are already synchronized, upload when local progress is further ahead, or apply remote when remote progress is further ahead.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode               | Behavior                                                                                                                     |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------- |
| **Dark** (default) | The CrossPoint logo on a dark background.                                                                                    |
| **Light**          | The CrossPoint logo on a white background.                                                                                   |
| **Custom**         | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found.                             |
| **Cover**          | The cover of the currently open book. Falls back to **Dark** if no book is open.                                             |
| **Cover + Custom** | The cover of the currently open book, shown only while actively reading. Falls back to **Custom** behavior when not reading. |
| **Transparent**    | A BMP or PNG overlay drawn over the current screen. Supports PNG and 32-bit BGRA alpha transparency, and treats white as transparent in regular BMPs. Falls back to **Dark** if no valid overlay image is found. |
| **None**           | A blank screen.                                                                                                              |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom** or **Cover + Custom**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. This takes priority over the `.sleep`/`sleep` directories.

#### Transparent overlay images

To use transparent sleep overlays, set the sleep screen mode to **Transparent**, then place BMP or PNG files on the SD card:

- **Multiple Images (recommended):** Create a `.sleep-overlay` directory in the root of the SD card and place any number of valid overlay `.bmp` or `.png` images inside. One will be randomly selected each time the device sleeps. A directory named `sleep-overlay` is also accepted as a fallback.
- **Single Image:** Place `sleep-overlay.bmp` or `sleep-overlay.png` in the root directory. A root BMP takes priority over a root PNG, and both take priority over the `.sleep-overlay`/`sleep-overlay` directories.

Transparent overlay files are intentionally separate from normal sleep images. Regular BMP formats supported by CrossPoint are accepted; white pixels leave the existing screen unchanged. For per-pixel alpha transparency, use a PNG with an alpha channel or a 32-bit BGRA BMP with both visible and non-opaque pixels. Opaque white pixels in alpha images erase the content behind them.

> [!TIP]
> For best results:
> - For non-transparent **Custom** mode, use uncompressed BMP files with 24-bit color depth.
> - For **Transparent** mode, use a PNG or uncompressed 32-bit BGRA BMP for per-pixel alpha, or a regular BMP for white-as-transparent artwork.
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

> [!TIP]
> You can set an image as the sleep screen cover directly from the BMP image viewer in the **[Browse Files](#33-browse-files-screen)** screen.

---

### 3.8 Custom Fonts (SD Card)

CrossPoint supports loading additional fonts from the SD card, extending beyond the two built-in families (Noto Serif, Noto Sans). Custom fonts can include extended Unicode coverage, enabling CJK (Chinese, Japanese, Korean) and other scripts.

There are three ways to install fonts:

1. **Download from device (recommended):** Go to **Settings -> System -> Manage Fonts**, browse the available font families, and select one to download over Wi-Fi.
2. **Upload via web interface:** While in **File Transfer** mode, open the web UI in a browser and navigate to the **Fonts** tab to upload `.cpfont` files.
3. **Manual SD card copy:** Download font files from the [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts) and copy them to `/.fonts/` (preferred) or `/fonts/` on your SD card.

Once installed, custom fonts appear in **Settings → Reader → Font Family** alongside the built-in fonts.

See [docs/sd-card-fonts.md](./docs/sd-card-fonts.md) for full installation details and SD card folder structure.

---

### 3.9 GitHub Library Sync

CrossPoint can mirror a private GitHub repository of books onto the SD card. You add, change, rename, move or delete books by pushing to the repository, then select **Update Library** on the **[Home](#31-home-screen)** screen to bring the card up to date. The sync is one-way (nothing is ever sent to GitHub) and only runs when you select **Update Library**. The entry appears only when the configuration file described below is present and valid.

#### What You Need

- A GitHub account.
- A private repository holding your books as plain files. Git LFS is not supported: with LFS, the reader would download small pointer files instead of your books.
- A computer with an SD card reader, for the one-time setup.
- A Wi-Fi network, either saved on the reader or entered when asked.

#### Creating the Access Token

The reader reads your repository with a fine-grained personal access token. On github.com:

1. Open **Settings → Developer settings → Personal access tokens → Fine-grained tokens**, then select **Generate new token**.
2. Give it a name, for example "CrossPoint reader".
3. **Expiration**: choose a date. 90 days or less is recommended (see the warning below). "No expiration" works, but is not recommended.
4. **Repository access**: choose **Only select repositories**, then select your books repository and nothing else.
5. **Permissions → Repository permissions**: set **Contents** to **Read-only**. GitHub adds **Metadata: Read-only** by itself; nothing else is needed.
6. Generate the token and copy it now: GitHub shows it only once.

If the repository belongs to an organization, the organization may have to approve fine-grained tokens before this one works.

> [!WARNING]
> The connection to GitHub is encrypted, but the reader does **not** check GitHub's certificate (the same as for OPDS, KOReader sync and the firmware update check). On a network you do not trust, someone in the middle could read your token. Because the token can only read this one repository, the damage is limited to someone reading your books. Protect yourself by giving the token an expiry date, syncing on Wi-Fi you trust, and revoking the token on GitHub (same page, **Revoke**) if you think it leaked.
>
> When the token expires, Update shows "Cannot read the repository. Check token, name and Wi-Fi." Generate a new token and replace the `token` value in the configuration file.

#### Writing the Configuration File

The sync is configured by one file on the SD card: `/.crosspoint/github-sync.json`. The token cannot be typed or edited on the device, so write this file from a computer.

> [!TIP]
> `.crosspoint` is a hidden folder on macOS (in Finder, press **Cmd+Shift+.** to show it) and on Linux. The reader creates it on first boot.

Copy this example and replace the three placeholders with your own values:

```json
{
  "owner": "your-github-username",
  "repo": "your-books-repository",
  "branch": "main",
  "token": "github_pat_PASTE_YOUR_TOKEN_HERE"
}
```

| Field    | Required | Rules                                                                                                                  |
| -------- | -------- | ---------------------------------------------------------------------------------------------------------------------- |
| `owner`  | yes      | The user or organization that owns the repository. Letters, digits and `-`; up to 39 characters.                      |
| `repo`   | yes      | The repository name. Letters, digits, `.`, `_` and `-`, but not `.` or `..`; up to 100 characters.                    |
| `branch` | no       | Defaults to `main`. Letters, digits, `.`, `_`, `/` and `-`, following git's branch-name rules; up to 255 characters. |
| `token`  | yes      | The token from the previous step. Visible ASCII characters, with no spaces and no `\`; up to 255 characters.           |

Behavior notes:

- The whole file must be at most 1 KB (1,024 bytes) of plain UTF-8 text. An empty file is invalid.
- Use a plain-text editor. Word processors, and macOS TextEdit in rich-text mode, turn `"` into curly quotes, which breaks the file.
- Each field must be a text value in quotes and appear only once. Other keys are ignored, as long as their value is not a nested object or list.
- When the file is missing or invalid, **Update Library** does not appear on the Home screen. An invalid file is reported on the serial log by a `GSYNC` line that names the problem and the field (see [Troubleshooting](#7-troubleshooting-issues--escaping-bootloop); `--filter GSYNC` shows only these lines). The token itself is never logged.
- The Home screen checks the file each time you return to it, so a new or fixed file makes the entry appear the next time you go back to the Home screen, without a restart.
- If the file becomes invalid after the entry was shown, selecting **Update Library** shows "Sync settings missing or invalid" and "Fix /.crosspoint/github-sync.json from a computer", and Wi-Fi is not started.

> [!NOTE]
> The **[File Transfer](#35-file-transfer-screen)** web interface and WebDAV refuse every path inside `.crosspoint` (and inside any folder whose name starts with `.`): the configuration file cannot be listed, downloaded, replaced, renamed, moved or deleted over the network. Write the file with the card in a computer. On the device, **Browse Files** shows `.crosspoint` only when **Settings → System → Show Hidden Files** is on.

#### Preparing the First Sync

The first sync can reuse the books you copy to the card yourself, so it does not have to download your whole library over Wi-Fi.

1. Write the configuration file (see above).
2. On your computer, get the repository's files (a fresh download from GitHub or an existing working copy) and **copy** them to the root of the SD card, keeping their folders. The repository's files always land at the root of the card: `Novels/Dune.epub` in the repository becomes `/Novels/Dune.epub` on the card. Files and folders whose name starts with `.` (such as `.git` or `.github`) do not need to be copied: the sync ignores them.
3. Do not `git clone` the repository onto the card. If you did, delete the `.git` folder from the card: it doubles the space used, and the reader changes files without updating it, so a later `git pull` on the card from a computer would refuse ("local changes would be overwritten"). A leftover `.git` folder is never touched by the sync.
4. Put the card back in the reader, return to the Home screen and select **Update Library**.
5. The first Update checks every copied file against the repository instead of downloading it, and counts it as **Already on card**. This reads the card, not the network, so it takes seconds to minutes rather than hours. A copied file that differs from the repository is replaced by the repository's version, and a repository file missing from the card is downloaded.

You can also skip step 2 and let the first Update download everything. This is slower and uses Wi-Fi for the whole library.

#### Running an Update

1. On the Home screen, select **Update Library**.
2. Wi-Fi works as in the OPDS browser: if the reader is not already connected, the network selection screen opens. If no connection is made, the screen shows "Wi-Fi connection failed".
3. The screen shows the current step: "Preparing...", "Reading the repository..." (with the number of files listed so far), then "Comparing with the SD card...". While files are processed, it shows a progress bar and `n/total : name`, under the current action: Moving, Checking, Downloading, Deleting, Removing empty folder or Skipping.
4. At the end, a summary is shown. Press **Back** or **Confirm** to return to the Home screen.

Behavior notes:

- **Back** stops the sync after the current file ("Stopping after the current file..."). On a touch device, the home gesture also stops the sync, then goes to the Home screen. Select **Update Library** again to resume: only what is left is done.
- The reader does not go to sleep during a sync.
- A power cut, a reset or a lost Wi-Fi connection never leaves a half-written book. Each download goes to a temporary file in `/.crosspoint/gitsync/` and replaces the book only once it is complete and verified. The next Update finishes the job.
- When you leave the sync screen and Wi-Fi was turned on, the reader restarts to the Home screen to free memory, as it does after OPDS browsing.

The summary screen shows:

- "Already up to date" when nothing had to change;
- otherwise "Library updated" with the **Added**, **Updated**, **Moved**, **Deleted** and **Already on card** counts;
- "Failed, retried next time" and "Skipped (name conflict)" with their counts, only when they are not zero.

A file counted as failed was not changed on the card, and is retried on the next Update. If the sync stopped early, the screen shows "Sync stopped" or "Sync failed" with the reason (see [Messages and What to Do](#messages-and-what-to-do)).

#### What Update Changes

- New files in the repository are downloaded.
- Changed files are replaced.
- Files deleted from the repository are deleted from the card.
- Renamed and moved files are renamed or moved on the card, without downloading them again.
- A moved or renamed book keeps its reading progress, bookmarks and Recent Books entry. If it was the book you had open, it is still the book that reopens at start-up.
- A book whose content changed in the repository starts again from the beginning, like a book re-uploaded through the web interface: its reading cache, which holds the reading position, is cleared. Its bookmarks are kept.
- After deletions, a folder left empty is removed only if a synced file lived in it.
- The reader keeps its own record of synced files in `/.crosspoint/gitsync/`.

#### What Update Never Touches

- Any file the sync did not put on the card or adopt: books copied over USB, downloaded from OPDS, uploaded through the web interface or sent from Calibre, screenshots, sleep images.
- Anything whose path contains a folder or file name starting with `.`, on the card or in the repository: `/.crosspoint/` (settings, reading progress, bookmarks), `/.fonts/`, `.git`, `.github`. Such repository files are not synced, so you can keep notes or CI files in the same repository.
- Folders that never held a synced file, even when they are empty.

> [!IMPORTANT]
> There is one exception: when the repository has a file at the same path as one of your own files, the repository's version replaces yours. The card does not distinguish upper and lower case, so `Books/Dune.epub` and `books/dune.EPUB` are the same file. Keep your own books in folders the repository does not use.

Update trusts its record of synced files. A synced book that you rename, move or delete on the device (with **Browse Files**, the web interface, or **Move Finished Books to Read Folder**) is not noticed, and is not put back until it changes in the repository. Make such changes in the repository instead, and turn **Settings → System → Move Finished Books to Read Folder** off if your library is synced.

#### Limits and Skipped Files

- **2,000 files**: a repository may hold up to 2,000 synced files. Files under folders starting with `.` do not count. A larger repository is refused with "Too many files (limit 2000)." and nothing is changed.
- **GitHub's list size**: GitHub lists at most 100,000 entries (7 MB of list) in one answer. Beyond that, the sync stops with "Repository too large for GitHub's file list." and nothing is changed.
- **Path length and form**: paths of up to 255 bytes are synced (UTF-8: an accented letter counts as 2 bytes). A path that is longer, has a file or folder name starting with a space, or contains a control character is ignored and logged.
- **Characters the card refuses**: a name containing `:`, `*`, `?`, `"`, `<`, `>` or `|` cannot exist on the card. Such files are skipped and counted under "Skipped (name conflict)". A name containing `\` is ignored and logged.
- **Names differing only by case**: when two repository paths differ only by upper and lower case, one is synced (the one already synced, otherwise the first in byte order). The others are skipped on every run, counted under "Skipped (name conflict)", and never deleted. Rename them in the repository.

#### Messages and What to Do

| Message                                                    | Meaning and what to do                                                                                                                                                                                     |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Already up to date                                         | Nothing to do.                                                                                                                                                                                             |
| Failed, retried next time: N                               | A download failed or arrived damaged. The file is retried on the next Update. If it keeps failing, check the file on GitHub.                                                                              |
| Skipped (name conflict): N                                 | Two names differ only by case, or a name has a character the card refuses. Rename the file in the repository (see [Limits and Skipped Files](#limits-and-skipped-files)).                                 |
| Sync stopped / Press Update again to resume.               | You pressed **Back**. Select **Update Library** to finish.                                                                                                                                                 |
| Network lost. Press Update again to resume.                | Downloads failed on three files in a row (Wi-Fi or GitHub). Move closer to the router and run Update again.                                                                                                |
| Cannot read the repository. Check token, name and Wi-Fi.   | Wrong `owner`, `repo` or `branch`; expired or revoked token; token without access to this repository; or no internet. Check the configuration file and the token.                                          |
| GitHub sent an invalid file list.                          | GitHub's answer was not what the reader expected, often because of a captive portal or proxy. Try again on another network.                                                                               |
| Repository too large for GitHub's file list.               | See [Limits and Skipped Files](#limits-and-skipped-files).                                                                                                                                                 |
| Too many files (limit 2000).                               | See [Limits and Skipped Files](#limits-and-skipped-files).                                                                                                                                                 |
| GitHub redirected to another site. Sync stopped.           | The reader refused to send your token to another site, and stopped. This should not happen with GitHub: sync on another network, and consider revoking the token.                                         |
| Sync record damaged. See the user guide.                   | Reset the sync record (see below) and run Update again.                                                                                                                                                    |
| SD card error. Check the card.                             | Check the card on a computer, then run Update again.                                                                                                                                                       |
| Not enough memory. Restart and try again.                  | Restart the reader and run Update before opening anything else.                                                                                                                                           |
| Sync settings missing or invalid                           | See [Writing the Configuration File](#writing-the-configuration-file).                                                                                                                                     |
| Wi-Fi connection failed                                    | No network was joined. Select **Update Library** again and choose a network.                                                                                                                               |

Files already processed before a sync stopped stay done; the next Update carries on from there.

**Resetting the sync record.** Delete the `/.crosspoint/gitsync/` folder from a computer. The next Update treats every repository file as in a first sync: files that are still identical are adopted, the others are downloaded. The cost: books deleted or moved in the repository since the last good sync stay on the card at their old path and are no longer managed by the sync. Delete them by hand.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning

| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Side Up**    |
| **Next Page**     | Press **Right** _or_ **Side Down** |

The role of the side buttons can be swapped in the **[Controls Settings](#363-controls)**.

If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation

* **Next Chapter:** Press and **hold** the **Right** (or **Side Down**) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Side Up**) button briefly, then release.

This feature can be disabled in the **[Controls Settings](#363-controls)** to help avoid changing chapters by mistake.

### Auto Page Turn

Auto Page Turn automatically advances pages at a set interval, useful for hands-free reading. This feature can be enabled and configured from the **[Reader Menu](#5-reader-menu)** while reading an EPUB.

### Tilt Page Turn (X3 only)

On the **Xteink X3**, the gyroscope can be used to turn pages by tilting the device. This feature is available in the Controls settings.

### Footnote Navigation

When reading an EPUB that contains footnotes, you can navigate to the footnote text by selecting the footnote reference in the book. From the footnote, you can return to your original reading position.

If the device goes to sleep or you close the book while viewing a footnote, the book reopens to your original reading position, not the footnote.

### Dictionary Lookup

Words on the current page can be looked up in an offline StarDict dictionary stored on the SD card. Copy a dictionary to the `/dictionaries/` folder, select it in **Settings → Reader → Dictionary**, then start a lookup by choosing **Look Up** in the **[Reader Menu](#5-reader-menu)** (or by holding **Confirm**, if the **Long-press Menu** setting in **[Controls Settings](#363-controls)** is set to "Dictionary"). Use **Left/Right** to highlight a word and press **Confirm** to show its definition.

See [docs/dictionary.md](docs/dictionary.md) for supported formats, setup, and where to find dictionaries.

### System Navigation

* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Return to Browse Files:** Press and hold the **Back** button to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen.
* **Reader Menu:** Press **Confirm** to open the **[Reader Menu](#5-reader-menu)**, which includes chapter navigation, reading options, and more.
* **Long-press Confirm (configurable):** Holding **Confirm** runs the function chosen by the **Long-press Menu** setting in **[Controls Settings](#363-controls)** — "Bookmark" (default) drops a bookmark, "KOSync" launches KOReader Sync, "Dictionary" starts a word lookup, "Disabled" does nothing. A short press always opens the Reader Menu.

### Supported Languages

CrossPoint renders text using the following Unicode character blocks, enabling support for a wide range of languages:

* **Latin Script (Basic, Supplement, Extended-A/B):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, Catalan, and others.
* **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.
* **Vietnamese:** Supported via extended Latin glyph coverage in the built-in reader fonts.

What is not supported with built-in reader fonts: Chinese, Japanese, Korean, Arabic, Greek, Hebrew, and Farsi. However, **CJK, Hebrew, Greek, and other extended scripts can be enabled by installing custom SD card fonts** — see [Custom Fonts (SD Card)](#38-custom-fonts-sd-card).

---

## 5. Reader Menu

Press **Confirm** while reading to open the Reader Menu. From here you can access reading utilities and navigation options without leaving the book.

Available options include:

- **Select Chapter** – Open the table of contents to jump to a specific chapter (see [Chapter Selection](#51-chapter-selection) below).
- **Footnotes** – Navigate to the footnotes for the current section *(only shown in books that contain footnotes)*.
- **Look Up** – Select a word on the current page and show its dictionary definition (see [docs/dictionary.md](docs/dictionary.md)). Requires a dictionary to be selected in **Settings → Reader → Dictionary**.
- **Reading Orientation** – Cycle through screen orientations without leaving the reader.
- **Auto Turn (Pages Per Minute)** – Cycle through automatic page turn speed options for hands-free reading.
- **Go to %** – Jump to a specific position in the book by percentage.
- **Take screenshot** – Save a screenshot of the current page to the `screenshots/` folder.
- **Show page as QR** – Display a QR code encoding the current reading position.
- **Go Home** – Close the book and return to the Home screen.
- **Sync Progress** – Push or pull reading progress with a KOReader sync server (see [KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)).
- **Delete Book Cache** – Clear the cached layout data for the current book, forcing a re-index on next open.

Press **Back** at any time to close the menu and return to your current page.

### 5.1 Chapter Selection

Accessible by selecting **Chapters** from the Reader Menu.

1. Use **Left** (or **Side Up**), or **Right** (or **Side Down**) to highlight the desired chapter.
2. Press **Confirm** to jump to that chapter.
3. *Alternatively, press **Back** to cancel and return to your current page.*

---

### 5.2 Bookmarks

Bookmarks can be created to quickly save and restore your place in a book.

To create a bookmark, hold **Confirm** for about half a second while inside a book. A popup will appear letting you know a bookmark was created. The popup message will automatically disappear in a couple of seconds.

To open bookmarks, press **Confirm** while inside a book. Then navigate to the **Bookmarks** menu. Bookmarks can be opened by navigating to them and pressing **Confirm**, which will redirect you to that place in the book. You can delete bookmarks by holding **Confirm** for about 0.7 seconds, and then pressing **Confirm** again to confirm deletion, or **Back** to cancel.

Bookmarks are stored in the `.crosspoint/bookmarks` folder in the JSON format.

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features are **not yet supported** but are planned for future updates:

* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.
* **Unsupported Image Formats:** Most JPG and PNG images in EPUBs render correctly. GIFs and progressive JPEGs are not supported and will fall back to an `[Image]` placeholder.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Crosspoint, feel free to raise an issue ticket and attach the logs.

**Crash reports on SD card:** After a crash, CrossPoint automatically saves a crash report to the SD card (no USB connection needed). Check the root of the SD card for a crash log file and include it with any bug report.

**Serial monitor logs:** For more detailed debugging, connect the device to a computer and run the custom debugging monitor script (requires Python 3 with `pyserial`, `colorama`, and `matplotlib`; install via `pip3 install pyserial colorama matplotlib`):

```
python3 scripts/debugging_monitor.py
```

The script auto-detects the serial port. You can also specify one explicitly:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

**Features:**

- Color-coded log output by category (errors, memory, display, EPUB parsing, etc.)
- Live memory usage graph (free RAM, total RAM, max contiguous allocation) updated every second
- Interactive command prompt — type a command and press Enter to send it to the device
- Screenshot capture — saves the current display to `screenshot.bmp` when triggered by the device

**Options:**

| Option               | Description                                               |
| -------------------- | --------------------------------------------------------- |
| `--baud RATE`        | Baud rate (default: 115200)                               |
| `--filter KEYWORD`   | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive)      |

**Examples:**

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.json`, `state.json`, or `epub_*` cache directories in the `.crosspoint/` folder). Deleting the whole `.crosspoint` directory also deletes the GitHub sync configuration (`github-sync.json`) and the sync record: keep a copy of the configuration file and put it back afterwards, and the next Update re-adopts the books already on the card (see **Resetting the sync record** in [Messages and What to Do](#messages-and-what-to-do)).
