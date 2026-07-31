use std::io::Cursor;
use std::net::TcpStream;
use std::sync::Mutex;
use std::time::Duration;
use tauri::{
    image::Image,
    menu::{MenuBuilder, MenuItemBuilder},
    tray::TrayIconBuilder,
    Manager, WebviewUrl, WebviewWindowBuilder,
};
use tauri_plugin_shell::process::CommandChild;
use tauri_plugin_shell::ShellExt;

fn decode_png_to_rgba(bytes: &[u8]) -> (Vec<u8>, u32, u32) {
    let decoder = png::Decoder::new(Cursor::new(bytes));
    let mut reader = decoder.read_info().expect("png read_info");
    let mut buf = vec![0; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).expect("png next_frame");
    let w = info.width;
    let h = info.height;
    let color_type = info.color_type;

    let rgba = match color_type {
        png::ColorType::Rgba => buf,
        png::ColorType::Rgb => {
            let mut out = Vec::with_capacity(w as usize * h as usize * 4);
            for chunk in buf.chunks(3) {
                out.extend_from_slice(chunk);
                out.push(255);
            }
            out
        }
        _ => panic!("formato PNG non supportato: {color_type:?}"),
    };

    (rgba, w, h)
}

fn main() {
    let app = tauri::Builder::default()
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            // ─── 1. MOSSA: File fisici in /tmp/ per libappindicator (Wayland SNI via DBus) ───
            let temp_dir = std::env::temp_dir();

            let tray_png_path = temp_dir.join("anidownloader-tray.png");
            let window_png_path = temp_dir.join("anidownloader-window.png");

            let _ = std::fs::write(&tray_png_path, include_bytes!("../icons/32x32.png"));
            let _ = std::fs::write(&window_png_path, include_bytes!("../icons/128x128.png"));

            // Decodifica PNG embedded → RGBA per Tauri Image
            let (tray_rgba, tw, th) =
                decode_png_to_rgba(include_bytes!("../icons/32x32.png"));
            let tray_image = Image::new_owned(tray_rgba, tw, th);

            let (win_rgba, ww, wh) =
                decode_png_to_rgba(include_bytes!("../icons/128x128.png"));
            let window_image = Image::new_owned(win_rgba, ww, wh);

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

            // ─── 2. MOSSA: icon_as_template(false) + with_id("main") ───
            TrayIconBuilder::with_id("main")
                .icon(tray_image)
                .icon_as_template(false)
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
                match app.shell().sidecar("anidownloaderd") {
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

            // ─── 3. MOSSA: Finestra con icona ───
            let _window = WebviewWindowBuilder::new(
                app,
                "main",
                WebviewUrl::App("index.html".into()),
            )
            .title("AniDownloader")
            .icon(window_image)?
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
                    if let Some(child) = guard.take() {
                        let _ = child.kill();
                    }
                }
            }
        }
    });
}

struct SidecarChild(Mutex<Option<CommandChild>>);
