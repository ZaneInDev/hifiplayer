package com.hifi.player

import android.content.pm.PackageManager
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.SeekBar
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.hifi.player.audio.PlaybackController
import com.hifi.player.audio.UsbAudioDeviceManager
import com.hifi.player.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var playbackController: PlaybackController
    private var isSeeking = false

    private val pickAudioLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let { loadAndPlay(it) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        requestAudioPermissionIfNeeded()

        playbackController = PlaybackController(this)
        wireCallbacks()
        wireControls()
        refreshUsbDeviceList()
    }

    private fun requestAudioPermissionIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ActivityCompat.checkSelfPermission(this, android.Manifest.permission.READ_MEDIA_AUDIO)
                != PackageManager.PERMISSION_GRANTED
            ) {
                ActivityCompat.requestPermissions(
                    this, arrayOf(android.Manifest.permission.READ_MEDIA_AUDIO), 100
                )
            }
        }
    }

    private fun wireCallbacks() {
        playbackController.onTrackLoaded = { track, routeDesc ->
            runOnUiThread {
                binding.textTitle.text = track.title
                binding.textArtist.text = track.artist
                binding.textRouteInfo.text = routeDesc
                binding.seekBar.max = track.durationMs.toInt().coerceAtLeast(1)
                playbackController.play()
            }
        }

        playbackController.onPlaybackStateChanged = { isPlaying ->
            runOnUiThread {
                binding.buttonPlayPause.text = if (isPlaying) "Pause" else "Play"
            }
        }

        playbackController.onProgressUpdate = { currentMs, totalMs ->
            runOnUiThread {
                if (!isSeeking) {
                    binding.seekBar.max = totalMs.toInt().coerceAtLeast(1)
                    binding.seekBar.progress = currentMs.toInt()
                }
                binding.textPosition.text = formatMs(currentMs)
                binding.textDuration.text = formatMs(totalMs)
            }
        }

        playbackController.onError = { message ->
            runOnUiThread {
                Toast.makeText(this, message, Toast.LENGTH_LONG).show()
            }
        }
    }

    private fun wireControls() {
        binding.buttonPickFile.setOnClickListener {
            pickAudioLauncher.launch(arrayOf("audio/*"))
        }

        binding.buttonPlayPause.setOnClickListener {
            if (playbackController.isPlaying()) {
                playbackController.pause()
            } else {
                playbackController.play()
            }
        }

        binding.seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) binding.textPosition.text = formatMs(progress.toLong())
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) { isSeeking = true }
            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                isSeeking = false
                seekBar?.let { playbackController.seekTo(it.progress.toLong()) }
            }
        })

        binding.seekBarVolume.max = 100
        binding.seekBarVolume.progress = 100
        binding.seekBarVolume.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) playbackController.setVolume(progress / 100f)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        binding.switchExclusiveMode.isChecked = true
        binding.switchExclusiveMode.setOnCheckedChangeListener { _, isChecked ->
            playbackController.exclusiveModeEnabled = isChecked
        }

        binding.buttonRefreshUsb.setOnClickListener { refreshUsbDeviceList() }
    }

    private fun refreshUsbDeviceList() {
        val dacs = playbackController.availableUsbDacs()
        val labels = mutableListOf("Internal DAC (default)")
        labels.addAll(dacs.map { "${it.productName} (USB)" })

        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)
        binding.spinnerUsbDevice.adapter = adapter

        binding.spinnerUsbDevice.setOnItemSelectedListener(object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                playbackController.preferredUsbDevice = if (position == 0) null else dacs[position - 1]
            }
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) {}
        })

        if (dacs.isNotEmpty()) {
            Toast.makeText(this, "${dacs.size} USB audio device(s) found", Toast.LENGTH_SHORT).show()
        }
    }

    private fun loadAndPlay(uri: Uri) {
        contentResolver.takePersistableUriPermission(
            uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION
        )

        val retriever = MediaMetadataRetriever()
        retriever.setDataSource(this, uri)
        val title = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE)
            ?: uri.lastPathSegment ?: "Unknown Title"
        val artist = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST) ?: "Unknown Artist"
        val duration = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
        retriever.release()

        val track = Track(uri = uri, title = title, artist = artist, durationMs = duration)
        playbackController.load(track)
    }

    private fun formatMs(ms: Long): String {
        val totalSeconds = ms / 1000
        val minutes = totalSeconds / 60
        val seconds = totalSeconds % 60
        return String.format("%d:%02d", minutes, seconds)
    }

    override fun onDestroy() {
        super.onDestroy()
        playbackController.release()
    }
}
