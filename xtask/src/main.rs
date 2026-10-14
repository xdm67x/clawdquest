//! Build helper for Clawd Pet (replaces the old Makefile).
//!
//! Run through the `xtask` cargo alias:
//!
//! ```sh
//! cargo xtask app [VERSION]   # build + bundle dist/ClawdQuest.app
//! cargo xtask dmg [VERSION]   # app + ClawdQuest-<VERSION>-macOS-arm64.dmg
//! ```
//!
//! VERSION defaults to the version in the root Cargo.toml; the release workflow
//! passes the tag instead.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const APP_NAME: &str = "Clawd Quest"; // display name / DMG volume
const APP_BUNDLE: &str = "ClawdQuest"; // bundle + DMG file stem
const BIN: &str = "clawdquest";
const ICON: &[u8] = include_bytes!("../../assets/AppIcon.icns");

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let cmd = args.first().map(String::as_str).unwrap_or("app");
    let version = args.get(1).cloned().unwrap_or_else(app_version);

    match cmd {
        "app" => {
            build();
            bundle(&version);
        }
        "dmg" => {
            build();
            let app = bundle(&version);
            dmg(&app, &version);
        }
        other => {
            eprintln!("xtask: unknown command {other:?} (expected `app` or `dmg`)");
            std::process::exit(2);
        }
    }
}

/// The workspace root (this crate lives in `<root>/xtask`).
fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("xtask has a parent directory")
        .to_path_buf()
}

/// The app version, read from the root package's Cargo.toml.
fn app_version() -> String {
    let manifest = fs::read_to_string(root().join("Cargo.toml")).unwrap_or_default();
    manifest
        .lines()
        .find_map(|l| l.trim().strip_prefix("version = \""))
        .and_then(|v| v.strip_suffix('"'))
        .unwrap_or("0.0")
        .to_string()
}

fn build() {
    let status = Command::new("cargo")
        .args(["build", "--release", "--package", "clawdquest"])
        .current_dir(root())
        .status()
        .expect("run cargo");
    assert!(status.success(), "cargo build --release failed");
}

/// Assemble dist/ClawdQuest.app and ad-hoc sign it; returns the bundle path.
fn bundle(version: &str) -> PathBuf {
    let root = root();
    let app = root.join("dist").join(format!("{APP_BUNDLE}.app"));
    let _ = fs::remove_dir_all(&app);
    let contents = app.join("Contents");
    let macos = contents.join("MacOS");
    let resources = contents.join("Resources");
    fs::create_dir_all(&macos).expect("create bundle dirs");
    fs::create_dir_all(&resources).expect("create bundle dirs");

    fs::copy(root.join("target/release").join(BIN), macos.join(BIN))
        .expect("copy release binary (run a release build first)");
    fs::write(resources.join("AppIcon.icns"), ICON).expect("write icon");
    fs::write(contents.join("Info.plist"), plist(version)).expect("write Info.plist");

    // Ad-hoc signature: enough for Gatekeeper to identify the bundle.
    let status = Command::new("codesign")
        .args(["--force", "--sign", "-"])
        .arg(&app)
        .status()
        .expect("run codesign");
    assert!(status.success(), "codesign failed");

    println!("bundled {}", app.display());
    app
}

fn dmg(app: &Path, version: &str) {
    let out = root().join(format!("{APP_BUNDLE}-{version}-macOS-arm64.dmg"));
    let _ = fs::remove_file(&out);
    let status = Command::new("hdiutil")
        .args(["create", "-volname", APP_NAME, "-srcfolder"])
        .arg(app)
        .args(["-format", "UDZO", "-ov"])
        .arg(&out)
        .status()
        .expect("run hdiutil");
    assert!(status.success(), "hdiutil failed");
    println!("created {}", out.display());
}

/// The bundle's Info.plist. LSUIElement keeps Clawd off the Dock and menu bar.
fn plist(version: &str) -> String {
    format!(
        r#"<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>
	<string>{APP_NAME}</string>
	<key>CFBundleDisplayName</key>
	<string>{APP_NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>app.clawdquest</string>
	<key>CFBundleExecutable</key>
	<string>{BIN}</string>
	<key>CFBundleIconFile</key>
	<string>AppIcon</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>{version}</string>
	<key>CFBundleVersion</key>
	<string>{version}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>LSMinimumSystemVersion</key>
	<string>11.0</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.utilities</string>
	<key>LSUIElement</key>
	<true/>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key>
	<true/>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>
"#
    )
}
