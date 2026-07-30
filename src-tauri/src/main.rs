use std::net::TcpStream;
use std::sync::Mutex;
use std::time::Duration;
use tauri::{
    menu::{MenuBuilder, MenuItemBuilder},
    tray::TrayIconBuilder,
    Manager, WebviewUrl, WebviewWindowBuilder,
};
use tauri_plugin_shell::process::CommandChild;
use tauri_plugin_shell::ShellExt;

fn main() {
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            // ─── Tray menu ───
            let show = MenuItemBuilder::with_id("show", "Mostra").build(app)?;
            let hide = MenuItemBuilder::with_id("hide", "Nascondi").build(app)?;
            let quit = MenuItemBuilder::with_id("quit", "Esci").build(app)?;

            let menu = MenuBuilder::new(app)
                .item(&show)
                .item(&hide)
                .separator()
                .item(&quit)
                .build()?;

            TrayIconBuilder::new()
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                    "hide" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.hide();
                        }
                    }
                    "quit" => {
                        app.exit(0);
                    }
                    _ => {}
                })
                .build(app)?;

            // ─── Sidecar: C++ backend (background) ───
            let port_in_use = TcpStream::connect_timeout(
                &"127.0.0.1:8989".parse().unwrap(),
                Duration::from_millis(200),
            )
            .is_ok();

            if port_in_use {
                println!("Sidecar già in ascolto sulla 8989, skip lancio");
                app.manage(SidecarChild(Mutex::new(None)));
            } else {
                match app.shell().sidecar("AniDownloader") {
                    Ok(cmd) => match cmd.args(["--web"]).spawn() {
                        Ok((_rx, child)) => {
                            println!("Sidecar C++ avviato con successo");
                            app.manage(SidecarChild(Mutex::new(Some(child))));
                        }
                        Err(e) => {
                            eprintln!("Errore nello spawn del sidecar: {e}");
                            app.manage(SidecarChild(Mutex::new(None)));
                        }
                    },
                    Err(e) => {
                        eprintln!("Errore nella creazione del comando sidecar: {e}");
                        app.manage(SidecarChild(Mutex::new(None)));
                    }
                }
            }

            // ─── Crea finestra webview → frontend Tauri ───
            let _window = WebviewWindowBuilder::new(
                app,
                "main",
                WebviewUrl::App("index.html".into()),
            )
            .title("AniDownloader")
            .inner_size(1100.0, 750.0)
            .min_inner_size(850.0, 600.0)
            .resizable(true)
            .center()
            .build()?;

            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building tauri application");

    app.run(|app_handle, event| {
        if let tauri::RunEvent::ExitRequested { .. } = event {
            if let Some(state) = app_handle.try_state::<SidecarChild>() {
                if let Ok(mut guard) = state.0.lock() {
                    if let Some(mut child) = guard.take() {
                        let _ = child.kill();
                    }
                }
            }
        }
    });
}

struct SidecarChild(Mutex<Option<CommandChild>>);
