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
                // One resolution entry per game: the manager turned the others off.
                if (wanted && box.dataset.res) {
                    document.querySelectorAll('input[data-res][data-tid="' + box.dataset.tid + '"]').forEach(function (other) {
                        if (other !== box) other.checked = false;
                    });
                }
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
