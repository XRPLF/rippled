use std::sync::atomic::{AtomicBool, Ordering};

use tracing::info;
use tracing_appender::non_blocking::WorkerGuard;
use tracing_subscriber::{EnvFilter, fmt};

static LOGGER_INITIALIZED: AtomicBool = AtomicBool::new(false);

#[cxx::bridge(namespace = "rs::hello_world")]
mod ffi {
    extern "Rust" {
        type LoggerGuard;
        fn init_logger() -> Box<LoggerGuard>;
        fn hello_world() -> String;
        fn log_info(s: &str);
    }
}

pub struct LoggerGuard(WorkerGuard);

pub fn init_logger() -> Box<LoggerGuard> {
    assert!(
        !LOGGER_INITIALIZED.swap(true, Ordering::SeqCst),
        "init_logger called more than once"
    );

    let (non_blocking, guard) = tracing_appender::non_blocking(std::io::stdout());

    let filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new("info"));

    fmt()
        .with_env_filter(filter)
        .with_writer(non_blocking)
        .init();

    Box::new(LoggerGuard(guard))
}

pub fn hello_world() -> String {
    "hello_world".to_string()
}

pub fn log_info(s: &str) {
    info!("{s}");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hello_world_returns_hello_world() {
        assert_eq!(hello_world(), "hello_world");
    }
}
