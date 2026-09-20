use std::thread;

fn main() {
    const NUM_THREADS: usize = 10;
    let mut handles = Vec::with_capacity(NUM_THREADS);
    for id in 0..NUM_THREADS {
        handles.push(thread::spawn(move || {
            println!("Hello from thread {id}");
        }));
    }
    for handle in handles {
        handle.join().expect("worker thread panicked");
    }

    println!("Hello from main thread");
}
