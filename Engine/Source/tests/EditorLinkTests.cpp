// The editor link over real sockets on 127.0.0.1, with both ends in this one program.
#include "BinaryArchive.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "EditorLink.h"
#include "EditorLinkLogSink.h"
#include "EditorLinkRecorder.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "NameComponent.h"
#include "NetConnection.h"
#include "TransformComponent.h"

#include <chrono>
#include <cstdint>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx::Engine;
	using namespace Nyx::Net;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}

	// Calls step until done() is true, for at most the time limit. Connecting to a port nobody
	// listens on takes about two seconds on Windows.
	bool PumpUntil(const std::function<void()>& step, const std::function<bool()>& done, int timeLimitMs = 10000)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs);
		while (std::chrono::steady_clock::now() < deadline)
		{
			step();
			if (done())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	std::vector<std::byte> MakeBytes(size_t size, uint32_t seed)
	{
		std::vector<std::byte> bytes(size);
		uint32_t value = seed;
		for (std::byte& byte : bytes)
		{
			value = value * 1664525u + 1013904223u;
			byte = static_cast<std::byte>(value >> 24);
		}
		return bytes;
	}

	// A message type the link doesn't know, like the ones later changes add
	struct TestMessage
	{
		static constexpr EEditorLinkMessage Type = static_cast<EEditorLinkMessage>(1000);

		uint32_t Value = 0;

		void Write(BinaryWriter& writer) const
		{
			writer.WriteUInt32(Value);
		}
	};

	struct ConnectedPair
	{
		std::unique_ptr<Connection> Client;
		std::unique_ptr<Connection> Server;
	};

	ConnectedPair Connect(Listener& listener)
	{
		ConnectedPair pair;
		pair.Client = Connection::ConnectToLocalPort(listener.GetPort());
		Require(PumpUntil(
					[&]
					{
						pair.Client->Poll();
						if (!pair.Server)
						{
							pair.Server = listener.Accept();
						}
					},
					[&] { return pair.Client->IsConnected() && pair.Server; }),
			"Connecting to the listener failed: " + pair.Client->GetCloseReason());
		return pair;
	}

	// A game's end connecting and an editor's end accepted from the listener, both updated
	struct LinkPair
	{
		std::unique_ptr<EditorLink> Game;
		std::unique_ptr<EditorLink> Editor;

		void Update(Listener& listener, const std::function<void(EditorLink&)>& onEditorCreated)
		{
			Game->Update();
			if (!Editor)
			{
				if (std::unique_ptr<Connection> accepted = listener.Accept())
				{
					Editor = std::make_unique<EditorLink>(std::move(accepted), "NyxEditor");
					onEditorCreated(*Editor);
				}
			}
			if (Editor)
			{
				Editor->Update();
			}
		}
	};

	LinkPair StartLinks(Listener& listener, const std::function<void(EditorLink&)>& onEditorCreated = [](EditorLink&) {})
	{
		LinkPair links;
		links.Game = std::make_unique<EditorLink>(Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame");
		Require(PumpUntil([&] { links.Update(listener, onEditorCreated); }, [&] { return links.Editor != nullptr; }),
			"The editor didn't accept the game");
		return links;
	}

	void TestStreamReaderPieces()
	{
		std::vector<std::byte> stream;
		AppendMessage(stream, 7, MakeBytes(300, 1));
		AppendMessage(stream, 8, {});
		AppendMessage(stream, 65535, MakeBytes(5, 2));

		// The header is little-endian: size 300 = 0x12C, then type 7
		Require(stream[0] == std::byte{ 0x2C } && stream[1] == std::byte{ 0x01 } && stream[2] == std::byte{ 0 } &&
				stream[3] == std::byte{ 0 } && stream[4] == std::byte{ 7 } && stream[5] == std::byte{ 0 },
			"The message header isn't size (u32) and type (u16), little-endian");

		// One byte at a time: a message only comes out once all of it has arrived
		MessageStreamReader reader;
		std::vector<Message> messages;
		for (const std::byte byte : stream)
		{
			reader.Append(&byte, 1);
			while (std::optional<Message> message = reader.TryTake())
			{
				messages.push_back(std::move(*message));
			}
		}

		Require(messages.size() == 3, "The stream reader didn't find three messages");
		Require(messages[0].Type == 7 && messages[0].Payload == MakeBytes(300, 1), "The first message changed");
		Require(messages[1].Type == 8 && messages[1].Payload.empty(), "The empty message changed");
		Require(messages[2].Type == 65535 && messages[2].Payload == MakeBytes(5, 2), "The third message changed");
		Require(reader.GetBufferedSize() == 0 && !reader.HasFailed(), "The stream reader kept bytes");
	}

	void TestStreamReaderRefusesHugeMessages()
	{
		// Text read as a header announces a payload of about 540 MB ("GET " as a little-endian u32).
		// The reader fails as soon as the header is complete, without waiting for the payload.
		const std::string notAMessage = "GET / HTTP/1.1\r\n\r\n";
		std::vector<std::byte> stream;
		AppendMessage(stream, 5, MakeBytes(20, 9));
		stream.insert(stream.end(), reinterpret_cast<const std::byte*>(notAMessage.data()),
			reinterpret_cast<const std::byte*>(notAMessage.data()) + notAMessage.size());

		MessageStreamReader reader;
		reader.Append(stream.data(), stream.size());
		Require(reader.HasFailed(), "The stream reader didn't notice the huge payload size");

		// The message before the bad header can still be taken, nothing after it
		const std::optional<Message> before = reader.TryTake();
		Require(before && before->Type == 5 && before->Payload == MakeBytes(20, 9), "The message before the bad header got lost");
		Require(!reader.TryTake(), "The stream reader read past the bad header");
	}

	void TestLargeMessagesAndOrder(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// Larger than the system's socket buffers, so sending and receiving take many steps
		const std::vector<std::byte> large = MakeBytes(8 * 1024 * 1024, 3);
		pair.Client->Send(1, MakeBytes(10, 4));
		pair.Client->Send(2, large);
		pair.Client->Send(3, {});

		std::vector<Message> received;
		Require(PumpUntil(
					[&]
					{
						pair.Client->Poll();
						pair.Server->Poll();
						while (std::optional<Message> message = pair.Server->Receive())
						{
							received.push_back(std::move(*message));
						}
					},
					[&] { return received.size() == 3; }),
			"Not all messages arrived");

		Require(received[0].Type == 1 && received[0].Payload == MakeBytes(10, 4), "The first message changed");
		Require(received[1].Type == 2 && received[1].Payload == large, "The 8 MB message changed");
		Require(received[2].Type == 3 && received[2].Payload.empty(), "The empty message changed");
		Require(pair.Client->GetUnsentSize() == 0, "Bytes are left to send");
	}

	void TestClosing(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// A message sent right before closing still arrives, followed by the close
		pair.Server->Send(42, MakeBytes(100, 5));
		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetUnsentSize() == 0; }), "Sending stalled");
		pair.Server->Close("test is done");
		Require(pair.Server->GetCloseCause() == ECloseCause::ThisSide && pair.Server->GetCloseReason() == "test is done",
			"Close() didn't record its reason");

		Require(PumpUntil([&] { pair.Client->Poll(); }, [&] { return pair.Client->GetState() == EConnectionState::Closed; }),
			"The client didn't notice the close");
		Require(pair.Client->GetCloseCause() == ECloseCause::OtherSide, "The close wasn't put down to the other side");

		const std::optional<Message> last = pair.Client->Receive();
		Require(last && last->Type == 42 && last->Payload == MakeBytes(100, 5), "The last message got lost in the close");

		// Sending on a closed connection is ignored
		pair.Client->Send(1, MakeBytes(10, 6));
		Require(pair.Client->GetUnsentSize() == 0, "A closed connection queued a message");
	}

	void TestClosingWithUnreadData(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);

		// The client closes while a message for it has arrived but hasn't been read. Unread bytes
		// would make the system reset the connection; Close() reads them first, so the server sees
		// a normal close, and the message can still be taken.
		pair.Server->Send(43, MakeBytes(50, 10));
		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetUnsentSize() == 0; }), "Sending stalled");
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		pair.Client->Close("test is done");

		Require(PumpUntil([&] { pair.Server->Poll(); }, [&] { return pair.Server->GetState() == EConnectionState::Closed; }),
			"The server didn't notice the close");
		Require(pair.Server->GetCloseCause() == ECloseCause::OtherSide,
			"Closing with unread data wasn't a normal close: " + pair.Server->GetCloseReason());

		const std::optional<Message> unread = pair.Client->Receive();
		Require(unread && unread->Type == 43 && unread->Payload == MakeBytes(50, 10), "The unread message got lost in the close");
	}

	void TestSendingTooLargeAMessage(Listener& listener)
	{
		ConnectedPair pair = Connect(listener);
		pair.Client->Send(1, std::vector<std::byte>(MaxMessagePayloadSize + 1));
		Require(pair.Client->GetState() == EConnectionState::Closed && pair.Client->GetCloseCause() == ECloseCause::Error &&
				Contains(pair.Client->GetCloseReason(), "larger than the limit"),
			"Sending a message over the limit didn't fail clearly: " + pair.Client->GetCloseReason());
	}

	uint16_t GetPortNobodyListensOn()
	{
		Listener probe;
		Require(probe.Listen(0), "Listening failed: " + probe.GetError());
		const uint16_t port = probe.GetPort();
		probe.Close();
		return port;
	}

	void TestConnectingToNobody()
	{
		const uint16_t port = GetPortNobodyListensOn();
		std::unique_ptr<Connection> connection = Connection::ConnectToLocalPort(port);
		Require(PumpUntil([&] { connection->Poll(); }, [&] { return connection->GetState() == EConnectionState::Closed; }),
			"Connecting to a port nobody listens on didn't fail");
		Require(connection->GetCloseCause() == ECloseCause::Error, "The failed connect wasn't an error");
		Require(connection->GetCloseReason().find("couldn't connect to port " + std::to_string(port)) == 0,
			"Unexpected reason: " + connection->GetCloseReason());

		// The link reports it, and is Closed
		EditorLink link(Connection::ConnectToLocalPort(port), "NyxGame");
		Require(PumpUntil([&] { link.Update(); }, [&] { return link.GetState() == EEditorLinkState::Closed; }),
			"The link didn't notice that connecting failed");
		Require(link.GetCloseReason().find("couldn't connect") == 0, "Unexpected reason: " + link.GetCloseReason());
	}

	// Returns this process's id, as the Hellos report it
	uint32_t TestHandshake(Listener& listener)
	{
		LinkPair links;
		links.Game = std::make_unique<EditorLink>(Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame");

		// Sent before the connection is established: it still goes out after the Hello
		links.Game->Send(TestMessage{ 7 });
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&] { return links.Game->IsConnected() && links.Editor && links.Editor->IsConnected(); }),
			"The handshake didn't finish");

		std::optional<Message> early;
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); }, [&] { return (early = links.Editor->Receive()).has_value(); }),
			"A message sent before the handshake didn't arrive");
		BinaryReader earlyReader;
		earlyReader.LoadFromMemory(early->Payload);
		uint32_t earlyValue = 0;
		Require(early->Type == 1000 && earlyReader.ReadUInt32(earlyValue) && earlyValue == 7, "A message sent before the handshake changed");

		const HelloMessage& editor = links.Game->GetOtherSide();
		const HelloMessage& game = links.Editor->GetOtherSide();
		Require(editor.ProgramName == "NyxEditor" && game.ProgramName == "NyxGame", "The program names didn't arrive");
		Require(editor.ProtocolVersion == EditorLinkProtocolVersion && game.ProtocolVersion == EditorLinkProtocolVersion,
			"The protocol versions didn't arrive");
		Require(editor.ProcessId != 0 && editor.ProcessId == game.ProcessId, "The Hellos don't carry this process's id");

		// Messages after the handshake are handed out, in order
		links.Editor->Send(TestMessage{ 1 });
		links.Editor->Send(TestMessage{ 2 });
		std::vector<Message> received;
		Require(PumpUntil(
					[&]
					{
						links.Update(listener, [](EditorLink&) {});
						while (std::optional<Message> message = links.Game->Receive())
						{
							received.push_back(std::move(*message));
						}
					},
					[&] { return received.size() == 2; }),
			"Messages after the handshake didn't arrive");

		for (uint32_t i = 0; i < 2; ++i)
		{
			BinaryReader reader;
			reader.LoadFromMemory(received[i].Payload);
			uint32_t value = 0;
			Require(received[i].Type == 1000 && reader.ReadUInt32(value) && value == i + 1, "A message changed on the way");
		}

		// A second Hello is a protocol error: the game closes the link, and the editor sees that
		links.Editor->Send(HelloMessage{});
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&]
					{
						return links.Game->GetState() == EEditorLinkState::Closed &&
							links.Editor->GetState() == EEditorLinkState::Closed;
					}),
			"A second Hello didn't close the link");
		Require(Contains(links.Game->GetCloseReason(), "said Hello a second time"), "Unexpected reason: " + links.Game->GetCloseReason());

		return editor.ProcessId;
	}

	void TestRequiredProcessId(Listener& listener, uint32_t thisProcessId)
	{
		{
			LinkPair links = StartLinks(listener, [&](EditorLink& editor) { editor.RequireOtherProcessId(thisProcessId); });
			Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
						[&] { return links.Game->IsConnected() && links.Editor->IsConnected(); }),
				"The link didn't accept the right process id");
		}

		LinkPair links = StartLinks(listener, [&](EditorLink& editor) { editor.RequireOtherProcessId(thisProcessId + 1); });
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&]
					{
						return links.Game->GetState() == EEditorLinkState::Closed &&
							links.Editor->GetState() == EEditorLinkState::Closed;
					}),
			"The link accepted another process");
		Require(!links.Editor->IsConnected() && Contains(links.Editor->GetCloseReason(), "is meant for process"),
			"Unexpected reason: " + links.Editor->GetCloseReason());
	}

	// The other end is a plain connection, to send what an EditorLink never would
	void TestBadHellos(Listener& listener)
	{
		// A newer protocol version
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");

			HelloMessage hello;
			hello.ProtocolVersion = EditorLinkProtocolVersion + 1;
			hello.ProgramName = "NyxGame";
			BinaryWriter writer;
			hello.Write(writer);
			pair.Client->Send(static_cast<uint16_t>(EEditorLinkMessage::Hello), writer.GetBytes());

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A different protocol version didn't close the link");
			Require(Contains(editor.GetCloseReason(), "NyxGame speaks editor link protocol " + std::to_string(EditorLinkProtocolVersion + 1)),
				"Unexpected reason: " + editor.GetCloseReason());

			// The editor said its own Hello before closing, and the other side can read it
			Require(PumpUntil([&] { pair.Client->Poll(); }, [&] { return pair.Client->GetState() == EConnectionState::Closed; }),
				"The other side didn't notice the close");
			const std::optional<Message> editorHello = pair.Client->Receive();
			Require(editorHello && editorHello->Type == static_cast<uint16_t>(EEditorLinkMessage::Hello), "The editor didn't say Hello");
			BinaryReader reader;
			reader.LoadFromMemory(editorHello->Payload);
			HelloMessage read;
			Require(read.Read(reader) && read.ProgramName == "NyxEditor", "The editor's Hello can't be read");
		}

		// Something other than Hello first
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");
			pair.Client->Send(99, MakeBytes(4, 7));

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A message before Hello didn't close the link");
			Require(Contains(editor.GetCloseReason(), "message type 99 before saying Hello"), "Unexpected reason: " + editor.GetCloseReason());
		}

		// A Hello too short to read
		{
			ConnectedPair pair = Connect(listener);
			EditorLink editor(std::move(pair.Server), "NyxEditor");
			pair.Client->Send(static_cast<uint16_t>(EEditorLinkMessage::Hello), MakeBytes(2, 8));

			Require(PumpUntil([&] { pair.Client->Poll(); editor.Update(); }, [&] { return editor.GetState() == EEditorLinkState::Closed; }),
				"A broken Hello didn't close the link");
			Require(Contains(editor.GetCloseReason(), "Hello can't be read"), "Unexpected reason: " + editor.GetCloseReason());
		}
	}

	template <typename TMessage>
	TMessage ReadAs(const Message& message)
	{
		Require(message.Type == static_cast<uint16_t>(TMessage::Type), "Unexpected message type " + std::to_string(message.Type));
		BinaryReader reader;
		reader.LoadFromMemory(message.Payload);
		TMessage read;
		Require(read.Read(reader), "A message can't be read");
		return read;
	}

	struct ObservedMessage
	{
		ELinkDirection Direction;
		uint16_t Type;
	};

	// The game sends log lines, the editor sends Quit, and the game closes gracefully: its last
	// line arrives, and the editor reports a normal end. Observers see every message both ways.
	void TestQuitAndLogLines(Listener& listener)
	{
		std::vector<ObservedMessage> seenByGame;
		std::vector<ObservedMessage> seenByEditor;

		LinkPair links;
		links.Game = std::make_unique<EditorLink>(Connection::ConnectToLocalPort(listener.GetPort()), "NyxGame",
			[&](ELinkDirection direction, const Message& message) { seenByGame.push_back({ direction, message.Type }); });

		Require(PumpUntil(
					[&]
					{
						links.Game->Update();
						if (!links.Editor)
						{
							if (std::unique_ptr<Connection> accepted = listener.Accept())
							{
								links.Editor = std::make_unique<EditorLink>(std::move(accepted), "NyxEditor",
									[&](ELinkDirection direction, const Message& message) { seenByEditor.push_back({ direction, message.Type }); });
							}
						}
						if (links.Editor)
						{
							links.Editor->Update();
						}
					},
					[&] { return links.Game->IsConnected() && links.Editor && links.Editor->IsConnected(); }),
			"The handshake didn't finish");

		LogLineMessage line;
		line.Level = ELogLevel::Warning;
		line.TimeMs = GetClockTimeMs();
		line.LoggerName = "APP";
		line.Text = "Something to look at";
		links.Game->Send(line);

		std::optional<Message> received;
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); }, [&] { return (received = links.Editor->Receive()).has_value(); }),
			"The log line didn't arrive");
		const LogLineMessage readLine = ReadAs<LogLineMessage>(*received);
		Require(readLine.Level == ELogLevel::Warning && readLine.TimeMs == line.TimeMs && readLine.LoggerName == "APP" &&
				readLine.Text == "Something to look at",
			"The log line changed on the way");

		// Quit: the game says its last line and closes gracefully, while the editor has a message
		// in flight to it that it won't read
		links.Editor->Send(QuitMessage{});
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); }, [&] { return (received = links.Game->Receive()).has_value(); }),
			"Quit didn't arrive");
		ReadAs<QuitMessage>(*received);

		links.Editor->Send(TestMessage{ 5 });
		LogLineMessage last = line;
		last.Text = "The last line";
		links.Game->Send(last);

		// Like the game's EditorLinkLayer: the editor answers the close while the game waits for it
		std::thread editorFrames([&]
			{
				PumpUntil([&] { links.Editor->Update(); }, [&] { return links.Editor->GetState() == EEditorLinkState::Closed; }, 5000);
			});
		links.Game->CloseGracefully("the game is closing", std::chrono::milliseconds(3000));
		editorFrames.join();

		Require(links.Editor->GetState() == EEditorLinkState::Closed, "The editor didn't notice the game closing");
		Require(links.Editor->GetCloseReason() == "the other side closed the connection",
			"The game's close wasn't a normal end: " + links.Editor->GetCloseReason());

		received = links.Editor->Receive();
		Require(received && ReadAs<LogLineMessage>(*received).Text == "The last line", "The game's last line got lost");

		// Hello first in both directions, then everything else, seen by both observers
		Require(seenByGame.size() >= 4 && seenByGame[0].Direction == ELinkDirection::Sent && seenByGame[0].Type == 1,
			"The game's observer didn't see its Hello first");
		Require(!seenByEditor.empty() && seenByEditor[0].Direction == ELinkDirection::Sent && seenByEditor[0].Type == 1,
			"The editor's observer didn't see its Hello first");

		const auto count = [](const std::vector<ObservedMessage>& seen, ELinkDirection direction, EEditorLinkMessage type)
		{
			size_t n = 0;
			for (const ObservedMessage& message : seen)
			{
				n += (message.Direction == direction && message.Type == static_cast<uint16_t>(type)) ? 1 : 0;
			}
			return n;
		};
		Require(count(seenByEditor, ELinkDirection::Received, EEditorLinkMessage::Hello) == 1 &&
				count(seenByEditor, ELinkDirection::Received, EEditorLinkMessage::LogLine) == 2 &&
				count(seenByEditor, ELinkDirection::Sent, EEditorLinkMessage::Quit) == 1,
			"The editor's observer missed messages");
		Require(count(seenByGame, ELinkDirection::Received, EEditorLinkMessage::Quit) == 1 &&
				count(seenByGame, ELinkDirection::Sent, EEditorLinkMessage::LogLine) == 2,
			"The game's observer missed messages");
	}

	void TestDescriptions()
	{
		const auto describe = [](auto message)
		{
			BinaryWriter writer;
			message.Write(writer);
			return DescribeEditorLinkMessage(Message{ static_cast<uint16_t>(decltype(message)::Type), writer.GetBytes() });
		};

		HelloMessage hello;
		hello.ProgramName = "NyxGame";
		hello.ProcessId = 1234;
		const EditorLinkMessageText helloText = describe(hello);
		Require(helloText.Name == "Hello" && helloText.Summary == "NyxGame, process 1234, protocol " + std::to_string(EditorLinkProtocolVersion) &&
				Contains(helloText.Details, "ProgramName: \"NyxGame\"") && Contains(helloText.Details, "ProcessId: 1234"),
			"Hello is described wrongly: " + helloText.Summary + " / " + helloText.Details);

		LogLineMessage line;
		line.Level = ELogLevel::Error;
		line.LoggerName = "ENGINE";
		line.Text = "two\nlines";
		const EditorLinkMessageText lineText = describe(line);
		Require(lineText.Name == "LogLine" && lineText.Summary == "[Error] ENGINE: two lines" &&
				Contains(lineText.Details, "Level: Error") && Contains(lineText.Details, "Text: \"two\nlines\""),
			"LogLine is described wrongly: " + lineText.Summary + " / " + lineText.Details);

		Require(describe(QuitMessage{}).Name == "Quit", "Quit is described wrongly");

		const EditorLinkMessageText unknown = DescribeEditorLinkMessage(Message{ 999, MakeBytes(3, 1) });
		Require(unknown.Name == "type 999" && Contains(unknown.Details, "(3 bytes)"), "An unknown type is described wrongly: " + unknown.Details);

		const EditorLinkMessageText broken = DescribeEditorLinkMessage(Message{ static_cast<uint16_t>(EEditorLinkMessage::LogLine), MakeBytes(3, 1) });
		Require(broken.Name == "LogLine" && Contains(broken.Summary, "can't be read"), "An unreadable LogLine is described wrongly");

		Require(FormatClockTime(GetClockTimeMs()).size() == 12, "The clock time isn't hh:mm:ss.mmm");
	}

	void TestRecording()
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() /
			("NyxEditorLinkTests-" + std::to_string(GetClockTimeMs()) + ".nyxlinklog");

		EditorLinkRecorder recorder;
		Require(recorder.Open(path, "NyxGame"), "The recording can't be created");

		HelloMessage hello;
		hello.ProgramName = "NyxGame";
		BinaryWriter writer;
		hello.Write(writer);
		recorder.Record(ELinkDirection::Sent, Message{ static_cast<uint16_t>(EEditorLinkMessage::Hello), writer.GetBytes() });
		recorder.Record(ELinkDirection::Received, Message{ static_cast<uint16_t>(EEditorLinkMessage::Quit), {} });
		recorder.Close();

		std::string text;
		Require(EditorLinkRecorder::PrintFile(path, text), "The recording can't be printed: " + text);
		Require(Contains(text, "Recorded by NyxGame") && Contains(text, "sent      Hello (") && Contains(text, "ProgramName: \"NyxGame\"") &&
				Contains(text, "received  Quit (0 bytes)") && Contains(text, "2 messages"),
			"The printed recording is missing something:\n" + text);

		// A recording cut off in the middle of a message, as after a crash, prints up to there
		const auto fullSize = std::filesystem::file_size(path);
		std::filesystem::resize_file(path, fullSize - 3);
		text.clear();
		Require(EditorLinkRecorder::PrintFile(path, text) && Contains(text, "ends in the middle of a message") &&
				Contains(text, "Hello ("),
			"A cut-off recording isn't printed up to the cut:\n" + text);

		// Something else isn't a recording
		{
			std::ofstream other(path, std::ios::binary | std::ios::trunc);
			other << "not a recording";
		}
		text.clear();
		Require(!EditorLinkRecorder::PrintFile(path, text) && Contains(text, "is not an editor link recording"),
			"Another file was taken for a recording");

		std::error_code ignored;
		std::filesystem::remove(path, ignored);
	}

	// Keeps the thousands of test lines out of the console; only the given sink gets them
	class QuietConsole
	{
	public:
		explicit QuietConsole(const std::shared_ptr<spdlog::sinks::sink>& keep)
		{
			Nyx::Core::Logger& logger = Nyx::Core::Logger::Get();
			for (const std::shared_ptr<spdlog::logger>& target : { logger.GetCoreLogger(), logger.GetClientLogger() })
			{
				for (const std::shared_ptr<spdlog::sinks::sink>& sink : target->sinks())
				{
					if (sink != keep)
					{
						Silenced.push_back({ sink, sink->level() });
						sink->set_level(spdlog::level::off);
					}
				}
			}
		}

		~QuietConsole()
		{
			for (const auto& [sink, level] : Silenced)
			{
				sink->set_level(level);
			}
		}

	private:
		std::vector<std::pair<std::shared_ptr<spdlog::sinks::sink>, spdlog::level::level_enum>> Silenced;
	};

	void TestLogSink()
	{
		const std::shared_ptr<EditorLinkLogSink> sink = EditorLinkLogSink::Install();
		Require(sink->TakeLines().empty(), "A new sink has lines");
		QuietConsole quiet(sink);

		// Lines from several threads all arrive, each thread's in order
		std::vector<std::thread> threads;
		for (int thread = 0; thread < 4; ++thread)
		{
			threads.emplace_back([thread]
				{
					for (int i = 0; i < 100; ++i)
					{
						LOG_INFO("thread {0} line {1}", thread, i);
					}
				});
		}
		for (std::thread& thread : threads)
		{
			thread.join();
		}

		std::vector<LogLineMessage> lines = sink->TakeLines();
		Require(lines.size() == 400, "Not all lines from the threads arrived: " + std::to_string(lines.size()));
		std::vector<int> nextLine(4, 0);
		for (const LogLineMessage& line : lines)
		{
			// "thread <t> line <i>"
			int thread = -1;
			int index = -1;
			const std::string& text = line.Text;
			const size_t lineWord = text.find(" line ");
			const bool bParsed = text.rfind("thread ", 0) == 0 && lineWord != std::string::npos &&
				std::from_chars(text.data() + 7, text.data() + lineWord, thread).ec == std::errc() &&
				std::from_chars(text.data() + lineWord + 6, text.data() + text.size(), index).ec == std::errc();
			Require(bParsed && thread >= 0 && thread < 4, "Unexpected line: " + line.Text);
			Require(index == nextLine[thread]++, "A thread's lines arrived out of order");
			Require(line.Level == ELogLevel::Info && line.LoggerName == "APP" && line.TimeMs != 0, "A line lost its level, logger or time");
		}

		// Nobody takes the lines: the oldest are kept, and the last line says how many were dropped
		for (size_t i = 0; i < EditorLinkLogSink::MaxKeptLines + 10; ++i)
		{
			CORE_LOG_TRACE("filler {0}", i);
		}
		lines = sink->TakeLines();
		Require(lines.size() == EditorLinkLogSink::MaxKeptLines + 1 && Contains(lines.back().Text, "10 log lines were dropped"),
			"Dropped lines aren't reported");
		Require(lines.front().LoggerName == "ENGINE" && lines.front().Level == ELogLevel::Trace, "Engine trace lines aren't kept");

		sink->Stop();
		LOG_INFO("after stopping");
		Require(sink->TakeLines().empty(), "A stopped sink still collects lines");
	}

	// Live edits, sent by the editor's end as typed messages and as messages queued before,
	// arrive in order and change the game's world
	void TestLiveEditsOverTheLink(Listener& listener)
	{
		Registry editorWorld;
		const Entity cube = editorWorld.CreateEntity();
		editorWorld.Add<NameComponent>(cube, NameComponent{ "Cube" });
		editorWorld.Add<GuidComponent>(cube, GuidComponent{ EntityGuid::Generate() });
		editorWorld.Add<TransformComponent>(cube, TransformComponent{});
		const EntityGuid cubeGuid = editorWorld.Get<GuidComponent>(cube).Guid;

		// Kept while the game connects, like the editor's edits during a game's startup
		std::vector<Message> queued;
		queued.push_back(MakeNetMessage(*MakeCreateEntityMessage(editorWorld, cube)));

		LinkPair links = StartLinks(listener);
		Require(PumpUntil([&] { links.Update(listener, [](EditorLink&) {}); },
					[&] { return links.Game->IsConnected() && links.Editor->IsConnected(); }),
			"The handshake didn't finish");

		for (const Message& message : queued)
		{
			links.Editor->Send(message);
		}

		editorWorld.Get<TransformComponent>(cube).Position = glm::vec3(4.0f, 5.0f, 6.0f);
		links.Editor->Send(*MakeSetPropertiesMessage(editorWorld, cube,
			*ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<TransformComponent>()), { 0, 1, 2 }));

		Registry gameWorld;
		size_t applied = 0;
		Require(PumpUntil(
					[&]
					{
						links.Update(listener, [](EditorLink&) {});
						while (std::optional<Message> message = links.Game->Receive())
						{
							Require(ApplyLiveEdit(*message, gameWorld, {}) == ELiveEditResult::Applied, "A live edit wasn't applied");
							++applied;
						}
					},
					[&] { return applied == 2; }),
			"The live edits didn't arrive");

		const std::optional<Entity> gameCube = FindEntityByGuid(gameWorld, cubeGuid);
		Require(gameCube && gameWorld.Get<NameComponent>(*gameCube).Name == "Cube" &&
				gameWorld.Get<TransformComponent>(*gameCube).Position == glm::vec3(4.0f, 5.0f, 6.0f),
			"The game's world doesn't show the live edits");

		links.Editor->Send(DeleteEntityMessage{ cubeGuid });
		Require(PumpUntil(
					[&]
					{
						links.Update(listener, [](EditorLink&) {});
						if (std::optional<Message> message = links.Game->Receive())
						{
							Require(ApplyLiveEdit(*message, gameWorld, {}) == ELiveEditResult::Applied, "Deleting wasn't applied");
						}
					},
					[&] { return !FindEntityByGuid(gameWorld, cubeGuid); }),
			"Deleting didn't arrive");

		links.Game->Close("done");
		links.Editor->Close("done");
	}

	void TestListenerWithoutWaitingPrograms(Listener& listener)
	{
		Require(listener.IsListening() && listener.GetPort() != 0, "The listener isn't listening");
		Require(listener.Accept() == nullptr, "Accept() returned a connection nobody made");
	}
}

int main()
{
	try
	{
		Nyx::Core::Logger::Get().Init();
		RegisterComponentTypes();

		TestStreamReaderPieces();
		TestStreamReaderRefusesHugeMessages();

		Listener listener;
		Require(listener.Listen(0), "Listening failed: " + listener.GetError());

		TestListenerWithoutWaitingPrograms(listener);
		TestLargeMessagesAndOrder(listener);
		TestClosing(listener);
		TestClosingWithUnreadData(listener);
		TestSendingTooLargeAMessage(listener);
		TestConnectingToNobody();
		const uint32_t thisProcessId = TestHandshake(listener);
		TestRequiredProcessId(listener, thisProcessId);
		TestBadHellos(listener);
		TestQuitAndLogLines(listener);
		TestLiveEditsOverTheLink(listener);
		TestDescriptions();
		TestRecording();
		TestLogSink();

		std::cout << "All editor link tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor link test failed: " << error.what() << '\n';
		return 1;
	}
}
