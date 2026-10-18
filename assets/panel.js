/* Keep actions and tab switches out of the console browser's Back history. */
document.addEventListener('DOMContentLoaded', function () {
    var pending = false;
    document.addEventListener('submit', function (event) {
        var form = event.target;
        if (form.method.toLowerCase() !== 'post') return;
        event.preventDefault();
        if (pending) return;
        pending = true;
        var button = event.submitter || document.activeElement;
        var action = button && button.form === form && button.hasAttribute('formaction')
            ? button.formAction : form.action;
        var body = new URLSearchParams(new FormData(form)).toString();
        fetch(action, {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: body,
            cache: 'no-store'
        }).then(function (response) {
            return response.text();
        }).then(function (html) {
            // Replace this document without creating a POST navigation/history entry.
            clearInterval(window.neoGpuTimer);
            document.open();
            document.write(html);
            document.close();
        }).catch(function () {
            pending = false;
            alert('Request interrupted. Check the current status before trying again.');
        });
    });
    // Game search: filters the list by name or title ID, ignoring case and accents.
    document.addEventListener('input', function (event) {
        if (event.target.id !== 'game-search') return;
        var fold = function (text) {
            return text.normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
        };
        var words = fold(event.target.value).split(/\s+/).filter(Boolean), shown = 0;
        document.querySelectorAll('article.game').forEach(function (game) {
            var text = fold(game.dataset.search || '');
            game.hidden = !words.every(function (word) { return text.indexOf(word) >= 0; });
            if (!game.hidden) shown++;
        });
        var none = document.getElementById('no-match');
        if (none) none.hidden = shown > 0;
    });
    // Patch checkboxes write the patch system settings at once; nothing to save.
    document.addEventListener('change', function (event) {
        var box = event.target;
        if (!box.dataset || !box.dataset.hash) return;
        var wanted = box.checked;
        box.disabled = true;
        fetch('/patches/set', {
            method: 'POST',
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: 'tid=' + box.dataset.tid + '&hash=' + box.dataset.hash + '&on=' + (wanted ? '1' : '0'),
            cache: 'no-store'
        }).then(function (response) {
            return response.text().then(function (text) {
                if (!response.ok) throw new Error(text);
                // The last line lists every entry of the game as hash=0/1 after the change: one resolution per
                // executable, and in a collection the eboot.bin resolution that the other executables need.
                var lines = text.split('\n'), state = lines.pop(), notice = false;
                if (state.indexOf('state') !== 0) return;
                state.split(' ').slice(1).forEach(function (pair) {
                    var kv = pair.split('='), other = document.querySelector('input[data-tid="' + box.dataset.tid + '"][data-hash="' + kv[0] + '"]');
                    if (!other || other.checked === (kv[1] === '1')) return;
                    other.checked = kv[1] === '1';
                    var sameGroup = other.dataset.res !== undefined && box.dataset.res !== undefined &&
                        other.dataset.res.toLowerCase() === box.dataset.res.toLowerCase();
                    if (other !== box && !(sameGroup && !other.checked)) notice = true;
                });
                if (notice) alert(lines.join('\n'));
            });
        }).catch(function (error) {
            box.checked = !wanted;
            alert(error.message || 'Request interrupted. Check the current status before trying again.');
        }).then(function () {
            box.disabled = false;
        });
    });
    document.addEventListener('click', function (event) {
        var link = event.target.closest('nav a');
        if (!link) return;
        event.preventDefault();
        window.location.replace(link.href);
    });
    document.addEventListener('keydown', function (event) {
        if (event.key === 'Escape' || event.key === 'BrowserBack' ||
            event.keyCode === 27 || event.keyCode === 461) {
            window.close();
            // Leave native Back handling available if the browser owns Circle.
        }
    });
});
