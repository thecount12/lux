// Simple client script that calls the Lux API and shows the response.
fetch('/api/hello')
  .then(function (res) { return res.json(); })
  .then(function (data) {
    document.getElementById('msg').textContent = JSON.stringify(data);
  })
  .catch(function (err) {
    console.error(err);
    document.getElementById('msg').textContent = 'Error fetching API';
  });

