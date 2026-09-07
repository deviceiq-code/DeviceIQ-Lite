// Shared by every authenticated page. The toast/confirm primitives and the
// idle-session watchdog are used everywhere; unlike DeviceIQ's version,
// there is no component-change poller here - Lite has no generic
// components API to poll, and its one stateful page (the dashboard) shows
// blind status inline already.
(function () {
    const TOAST_DURATION_MS = 5000;

    let toastContainer = null;
    let modalOverlay = null;

    function ensureToastContainer() {
        if (toastContainer) return toastContainer;
        toastContainer = document.createElement("div");
        toastContainer.className = "toast-container";
        document.body.appendChild(toastContainer);
        return toastContainer;
    }

    // options.title is shown above the message; toasts stack (each call
    // adds one to the container) and can be dismissed early via the close
    // button, in addition to auto-dismissing after TOAST_DURATION_MS.
    function show(message, options) {
        options = options || {};
        const container = ensureToastContainer();

        const toast = document.createElement("div");
        toast.className = "toast";

        const content = document.createElement("div");
        content.className = "toast-content";
        if (options.title) {
            const title = document.createElement("div");
            title.className = "toast-title";
            title.textContent = options.title;
            content.appendChild(title);
        }
        const messageEl = document.createElement("div");
        messageEl.className = "toast-message";
        messageEl.textContent = message;
        content.appendChild(messageEl);

        const closeButton = document.createElement("button");
        closeButton.type = "button";
        closeButton.className = "toast-close";
        closeButton.setAttribute("aria-label", "Dismiss");
        closeButton.textContent = "×";

        toast.appendChild(content);
        toast.appendChild(closeButton);
        container.appendChild(toast);

        let dismissTimer = null;
        function dismiss() {
            if (dismissTimer) clearTimeout(dismissTimer);
            toast.classList.remove("visible");
            setTimeout(function () { toast.remove(); }, 300);
        }
        closeButton.addEventListener("click", dismiss);

        requestAnimationFrame(function () { toast.classList.add("visible"); });
        dismissTimer = setTimeout(dismiss, TOAST_DURATION_MS);
    }

    function ensureModal() {
        if (modalOverlay) return modalOverlay;
        modalOverlay = document.createElement("div");
        modalOverlay.className = "modal-overlay";
        modalOverlay.innerHTML =
            '<div class="modal-box">' +
                '<div class="modal-message"></div>' +
                '<input class="modal-input" type="text" hidden>' +
                '<div class="modal-actions">' +
                    '<button class="secondary" data-role="cancel"></button>' +
                    '<button data-role="confirm"></button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(modalOverlay);
        return modalOverlay;
    }

    // Resolves true if the user confirmed, false if cancelled or dismissed.
    function confirmDialog(message, options) {
        options = options || {};
        const overlay = ensureModal();
        const confirmButton = overlay.querySelector('[data-role="confirm"]');
        const cancelButton = overlay.querySelector('[data-role="cancel"]');
        overlay.querySelector(".modal-input").hidden = true;
        overlay.querySelector(".modal-message").textContent = message;
        confirmButton.textContent = options.confirmLabel || "OK";
        cancelButton.textContent = options.cancelLabel || "Cancel";

        return new Promise(function (resolve) {
            function cleanup(result) {
                overlay.classList.remove("visible");
                confirmButton.removeEventListener("click", onConfirm);
                cancelButton.removeEventListener("click", onCancel);
                overlay.removeEventListener("click", onOverlayClick);
                resolve(result);
            }
            function onConfirm() { cleanup(true); }
            function onCancel() { cleanup(false); }
            function onOverlayClick(event) { if (event.target === overlay) cleanup(false); }

            confirmButton.addEventListener("click", onConfirm);
            cancelButton.addEventListener("click", onCancel);
            overlay.addEventListener("click", onOverlayClick);
            overlay.classList.add("visible");
        });
    }

    // Resolves the entered text if confirmed, or null if cancelled/dismissed.
    function promptDialog(message, options) {
        options = options || {};
        const overlay = ensureModal();
        const confirmButton = overlay.querySelector('[data-role="confirm"]');
        const cancelButton = overlay.querySelector('[data-role="cancel"]');
        const input = overlay.querySelector(".modal-input");
        overlay.querySelector(".modal-message").textContent = message;
        confirmButton.textContent = options.confirmLabel || "OK";
        cancelButton.textContent = options.cancelLabel || "Cancel";
        input.type = options.inputType || "text";
        input.value = options.defaultValue || "";
        input.hidden = false;

        return new Promise(function (resolve) {
            function cleanup(result) {
                overlay.classList.remove("visible");
                input.hidden = true;
                confirmButton.removeEventListener("click", onConfirm);
                cancelButton.removeEventListener("click", onCancel);
                overlay.removeEventListener("click", onOverlayClick);
                input.removeEventListener("keydown", onKeydown);
                resolve(result);
            }
            function onConfirm() { cleanup(input.value); }
            function onCancel() { cleanup(null); }
            function onOverlayClick(event) { if (event.target === overlay) cleanup(null); }
            function onKeydown(event) { if (event.key === "Enter") { event.preventDefault(); cleanup(input.value); } }

            confirmButton.addEventListener("click", onConfirm);
            cancelButton.addEventListener("click", onCancel);
            overlay.addEventListener("click", onOverlayClick);
            input.addEventListener("keydown", onKeydown);
            overlay.classList.add("visible");
            input.focus();
        });
    }

    // --- Idle session watchdog --------------------------------------------
    // The server just lets the session cookie expire silently after
    // SESSION_IDLE_TIMEOUT_MS (main.cpp's FindSession()), and the user only
    // finds out on their next click, via a redirect back to the login page.
    // This makes that moment visible instead: it warns shortly before the
    // same deadline and, if there's still no response, logs out client-side
    // rather than leaving a "logged in" page sitting on a session the
    // server has already dropped.
    const IDLE_ACTIVITY_EVENTS = ["mousedown", "mousemove", "keydown", "wheel", "touchstart"];
    const IDLE_CHECK_INTERVAL_MS = 1000;
    const IDLE_MIN_WARNING_MS = 10000;
    const IDLE_MAX_WARNING_MS = 60000;

    let idleTimeoutMs = 0;
    let idleWarningMs = 0;
    let idleLastActivityAt = 0;
    let idleCheckTimer = null;
    let idleOverlay = null;
    let idleWarningShown = false;

    function ensureIdleOverlay() {
        if (idleOverlay) return idleOverlay;
        idleOverlay = document.createElement("div");
        idleOverlay.className = "modal-overlay";
        idleOverlay.innerHTML =
            '<div class="modal-box">' +
                '<div class="modal-message"></div>' +
                '<div class="modal-actions">' +
                    '<button data-role="stay">Stay signed in</button>' +
                '</div>' +
            '</div>';
        document.body.appendChild(idleOverlay);
        idleOverlay.querySelector('[data-role="stay"]').addEventListener("click", function () {
            idleMarkActivity();
        });
        return idleOverlay;
    }

    function hideIdleWarning() {
        if (idleOverlay) idleOverlay.classList.remove("visible");
        idleWarningShown = false;
    }

    function showIdleWarning(remainingMs) {
        const overlay = ensureIdleOverlay();
        const seconds = Math.max(1, Math.ceil(remainingMs / 1000));
        overlay.querySelector(".modal-message").textContent =
            "You've been inactive for a while - this session will end in " + seconds + "s.";
        overlay.classList.add("visible");
        idleWarningShown = true;
    }

    function idleMarkActivity() {
        idleLastActivityAt = Date.now();
        if (idleWarningShown) hideIdleWarning();
    }

    function expireIdleSession() {
        stopIdleWatchdog();
        // keepalive so the request survives the navigation that follows -
        // same reasoning as Notifications.reboot() below.
        fetch("/api/logout", { method: "POST", keepalive: true }).catch(function () {});
        window.location.href = "/";
    }

    function checkIdle() {
        const remaining = idleTimeoutMs - (Date.now() - idleLastActivityAt);
        if (remaining <= 0) {
            expireIdleSession();
        } else if (remaining <= idleWarningMs) {
            showIdleWarning(remaining);
        } else if (idleWarningShown) {
            hideIdleWarning();
        }
    }

    function stopIdleWatchdog() {
        if (idleCheckTimer) clearInterval(idleCheckTimer);
        idleCheckTimer = null;
        IDLE_ACTIVITY_EVENTS.forEach(function (name) { document.removeEventListener(name, idleMarkActivity); });
        hideIdleWarning();
    }

    // valueMs is the server's configured session idle timeout
    // (SESSION_IDLE_TIMEOUT_MS in main.cpp, from GET /api/session). 0
    // disables it, same convention as the server side.
    function startIdleWatchdog(valueMs) {
        stopIdleWatchdog();
        idleTimeoutMs = valueMs | 0;
        if (idleTimeoutMs <= 0) return;

        idleWarningMs = Math.min(IDLE_MAX_WARNING_MS, Math.max(IDLE_MIN_WARNING_MS, Math.floor(idleTimeoutMs * 0.2)));
        idleLastActivityAt = Date.now();
        IDLE_ACTIVITY_EVENTS.forEach(function (name) { document.addEventListener(name, idleMarkActivity, { passive: true }); });
        idleCheckTimer = setInterval(checkIdle, IDLE_CHECK_INTERVAL_MS);
    }

    // Every authenticated page includes this script, so this is the one
    // place that needs to know the timeout - no per-page wiring required.
    fetch("/api/session").then(function (response) {
        return response.ok ? response.json() : null;
    }).then(function (session) {
        if (session && session.authenticated) startIdleWatchdog(session.idleTimeoutMs);
    }).catch(function () {});

    window.Notifications = {
        show: show,
        confirm: confirmDialog,
        prompt: promptDialog,
        // Navigates to the dedicated "restarting..." page, which waits for
        // the device to come back online and then sends the browser to the
        // login page - used everywhere a restart is triggered, instead of
        // just toasting and leaving the user on a page that's about to go
        // unreachable.
        restarting: function () {
            window.location.href = "/restarting.html";
        },
        // POSTs /api/restart and then shows the restarting page. A plain
        // fetch() immediately followed by a navigation can get cancelled by
        // the browser before it ever reaches the device - especially over
        // this device's slow, single-connection web server - which looked
        // like "Restart doesn't actually restart" even though the button
        // worked. keepalive keeps the request alive across the navigation
        // that follows.
        reboot: function () {
            fetch("/api/restart", { method: "POST", keepalive: true }).catch(function () {});
            this.restarting();
        }
    };
})();
