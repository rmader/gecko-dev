/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Checks that opening the Library view using the default toolbar button works
 * also while the view is displayed in the main menu.
 */
add_task(async function test_library_after_appMenu() {
  if (PanelUI.protonAppMenuEnabled) {
    Assert.ok(
      true,
      "Skipping test because the Library is no longer in the Proton AppMenu."
    );
    return;
  }
  await gCUITestUtils.openMainMenu();

  // Show the Library view as a subview of the main menu.
  document.getElementById("appMenu-library-button").click();
  let libraryView = document.getElementById("appMenu-libraryView");
  let promise = BrowserTestUtils.waitForEvent(libraryView, "ViewShown");
  await promise;

  // Show the Library view as the main view of the Library panel.
  promise = BrowserTestUtils.waitForEvent(libraryView, "ViewShown");
  document.getElementById("library-button").click();
  await promise;

  // Navigate to the History subview.
  document.getElementById("appMenu-library-history-button").click();
  let historyView = document.getElementById("PanelUI-history");
  promise = BrowserTestUtils.waitForEvent(historyView, "ViewShown");
  await promise;

  Assert.ok(PanelView.forNode(historyView).active);

  // Close the Library panel.
  let historyPanel = historyView.closest("panel");
  promise = BrowserTestUtils.waitForEvent(historyPanel, "popuphidden");
  historyPanel.hidePopup();
  await promise;
});
