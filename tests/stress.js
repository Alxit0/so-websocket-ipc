import http from 'k6/http';
import { check } from 'k6';

export let options = {
  stages: [
    { duration: '20s', target: 50 },   // ramp up
    { duration: '40s', target: 200 },  // peak load
    { duration: '20s', target: 0 },    // ramp down
  ]
};

export default function () {
  const r = http.get('http://localhost:8080/');
  check(r, { 'status == 200': (res) => res.status === 200 });
}
