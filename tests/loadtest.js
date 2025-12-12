import http from 'k6/http';
import { sleep } from 'k6';

export const options = {
    vus: 100,              // Number of concurrent users
    duration: '30s',       // Total test duration
    thresholds: {
        http_req_duration: ['p(95)<200'],   // 95% of requests under 200ms
        http_req_failed: ['rate<0.01'],     // < 1% failures
    },
};

export default function () {
    const url = 'http://localhost:8080/index.html';

    // You can add multiple files for more realistic tests
    const responses = http.batch([
        ['GET', url],
		['GET', 'http://localhost:8080/index42.html'], // 404 test
        ['GET', 'http://localhost:8080/style.css'],
        ['GET', 'http://localhost:8080/script.js'],
        ['GET', 'http://localhost:8080/image.jpg'],
    ]);

    sleep(1);   // Wait 1 second per VU iteration
}
