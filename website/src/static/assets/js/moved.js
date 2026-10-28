// Old Console URL: keep the query and hash (e.g. #library) when moving to /profile.
location.replace('profile' + location.search + location.hash);
