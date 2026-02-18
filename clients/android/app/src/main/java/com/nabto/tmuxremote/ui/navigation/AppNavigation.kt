package com.nabto.tmuxremote.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.nabto.tmuxremote.patterns.PatternEngine
import com.nabto.tmuxremote.services.BookmarkStore
import com.nabto.tmuxremote.services.ConnectionManager
import com.nabto.tmuxremote.services.NabtoService
import com.nabto.tmuxremote.ui.screens.DeviceListScreen
import com.nabto.tmuxremote.ui.screens.PairingScreen
import com.nabto.tmuxremote.ui.screens.SettingsScreen
import com.nabto.tmuxremote.ui.screens.TerminalScreen

sealed class Screen(val route: String) {
    data object DeviceList : Screen("device_list")
    data object Pairing : Screen("pairing")
    data object Settings : Screen("settings")
    data object Terminal : Screen("terminal/{deviceId}/{session}") {
        fun createRoute(deviceId: String, session: String): String =
            "terminal/$deviceId/$session"
    }
}

@Composable
fun AppNavigation(
    nabtoService: NabtoService,
    connectionManager: ConnectionManager,
    bookmarkStore: BookmarkStore
) {
    val navController = rememberNavController()
    val devices by bookmarkStore.devices.collectAsState()

    // Determine start destination based on resume logic
    val startDestination = resolveStartDestination(bookmarkStore)

    NavHost(navController = navController, startDestination = startDestination) {

        composable(Screen.DeviceList.route) {
            DeviceListScreen(
                nabtoService = nabtoService,
                connectionManager = connectionManager,
                bookmarkStore = bookmarkStore,
                onNavigateToTerminal = { deviceId, session ->
                    navController.navigate(Screen.Terminal.createRoute(deviceId, session))
                },
                onNavigateToPairing = {
                    navController.navigate(Screen.Pairing.route)
                },
                onNavigateToSettings = {
                    navController.navigate(Screen.Settings.route)
                }
            )
        }

        composable(Screen.Pairing.route) {
            PairingScreen(
                nabtoService = nabtoService,
                bookmarkStore = bookmarkStore,
                onDismiss = { navController.popBackStack() }
            )
        }

        composable(Screen.Settings.route) {
            SettingsScreen(
                onDismiss = { navController.popBackStack() }
            )
        }

        composable(
            route = Screen.Terminal.route,
            arguments = listOf(
                navArgument("deviceId") { type = NavType.StringType },
                navArgument("session") { type = NavType.StringType }
            )
        ) { backStackEntry ->
            val deviceId = backStackEntry.arguments?.getString("deviceId") ?: return@composable
            val session = backStackEntry.arguments?.getString("session") ?: return@composable
            val bookmark = bookmarkStore.bookmark(deviceId) ?: run {
                navController.popBackStack()
                return@composable
            }

            TerminalScreen(
                bookmark = bookmark,
                sessionName = session,
                nabtoService = nabtoService,
                connectionManager = connectionManager,
                bookmarkStore = bookmarkStore,
                onDismiss = {
                    navController.popBackStack(Screen.DeviceList.route, inclusive = false)
                }
            )
        }
    }
}

private fun resolveStartDestination(bookmarkStore: BookmarkStore): String {
    val lastDeviceId = bookmarkStore.lastDeviceId ?: return Screen.DeviceList.route
    val bookmark = bookmarkStore.bookmark(lastDeviceId) ?: return Screen.DeviceList.route
    val session = bookmark.lastSession ?: return Screen.DeviceList.route
    return Screen.Terminal.createRoute(bookmark.deviceId, session)
}
